#include <array>

#include <qdir.h>
#include <qfileinfo.h>
#include <qlogging.h>
#include <qstring.h>

#include <windows.h>

#include <objbase.h>
#include <objidl.h>
#include <shlobj.h>
#include <shobjidl.h>

#include "desktopentry.hpp"

namespace {

// COM on whichever thread scans: the constructor's first scan runs on the GUI
// thread, rescans on the thread pool.
class ComScope {
public:
	ComScope(): ok(SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {}
	~ComScope() {
		if (this->ok) CoUninitialize();
	}
	ComScope(const ComScope&) = delete;
	ComScope(ComScope&&) = delete;
	ComScope& operator=(const ComScope&) = delete;
	ComScope& operator=(ComScope&&) = delete;

private:
	bool ok;
};

} // namespace

// The entry runs the shortcut itself through Explorer, as a double click does:
// advertised (MSI) shortcuts have no target path, and Explorer applies the
// shortcut's own working directory (which may hold unexpanded variables, so it
// isn't copied out), arguments and run-as settings.
// startupClass is the target's file name without .exe, which is what window
// managers such as komorebi report as an app id, so heuristicLookup finds it.
// Shortcuts to anything but a program (readmes, help, links) and uninstallers
// are NoDisplay: byId still finds them.
ParsedDesktopEntryData DesktopEntry::parseShortcut(const QString& id, const QString& path) {
	auto data = ParsedDesktopEntryData();
	data.id = id;
	data.name = QFileInfo(path).completeBaseName();

	auto nativePath = QDir::toNativeSeparators(path);
	data.command = {QStringLiteral("explorer.exe"), nativePath};

	ComScope com;
	IShellLinkW* link = nullptr;
	if (FAILED(CoCreateInstance(
	        CLSID_ShellLink,
	        nullptr,
	        CLSCTX_INPROC_SERVER,
	        IID_IShellLinkW,
	        reinterpret_cast<void**>(&link) // NOLINT
	    )))
	{
		return data;
	}

	IPersistFile* file = nullptr;
	if (SUCCEEDED(link->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file)))) { // NOLINT
		if (SUCCEEDED(file->Load(reinterpret_cast<LPCOLESTR>(nativePath.utf16()), STGM_READ))) { // NOLINT
			auto buffer = std::array<wchar_t, 1024> {};

			// SLGP_RAWPATH keeps environment variables unexpanded, so not that.
			if (SUCCEEDED(link->GetPath(buffer.data(), static_cast<int>(buffer.size()), nullptr, 0))) {
				auto target = QString::fromWCharArray(buffer.data());
				buffer.fill(0);
				if (SUCCEEDED(link->GetArguments(buffer.data(), static_cast<int>(buffer.size())))) {
					auto arguments = QString::fromWCharArray(buffer.data());
					data.execString = arguments.isEmpty() ? target : target + ' ' + arguments;
				}

				auto info = QFileInfo(target);
				if (info.suffix().compare("exe", Qt::CaseInsensitive) == 0) {
					data.startupClass = info.completeBaseName();
				} else if (!target.isEmpty()) {
					data.noDisplay = true;
				}
			}

			buffer.fill(0);
			if (SUCCEEDED(link->GetDescription(buffer.data(), static_cast<int>(buffer.size())))) {
				data.comment = QString::fromWCharArray(buffer.data());
			}
		} else {
			qDebug() << "Could not load shortcut" << path;
		}

		file->Release();
	}

	link->Release();

	if (data.name.contains(QStringLiteral("uninstall"), Qt::CaseInsensitive)) data.noDisplay = true;

	return data;
}
