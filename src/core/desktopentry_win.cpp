#include <array>

#include <qdir.h>
#include <qdiriterator.h>
#include <qfileinfo.h>
#include <qhash.h>
#include <qlogging.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qurl.h>

#include <windows.h>

#include <objbase.h>
#include <objidl.h>
#include <shlobj.h>
#include <shobjidl.h>

// Defines the PKEY_* used here rather than declaring them extern.
#include <initguid.h>

#include <propkey.h>

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

QString displayName(IShellItem* item, SIGDN form) {
	PWSTR name = nullptr;
	if (FAILED(item->GetDisplayName(form, &name))) return QString();
	auto result = QString::fromWCharArray(name);
	CoTaskMemFree(name);
	return result;
}

QString stringProperty(IShellItem* item, const PROPERTYKEY& key) {
	IShellItem2* item2 = nullptr;
	if (FAILED(item->QueryInterface(IID_PPV_ARGS(&item2)))) return QString();

	auto result = QString();
	PWSTR value = nullptr;
	if (SUCCEEDED(item2->GetString(key, &value))) {
		result = QString::fromWCharArray(value);
		CoTaskMemFree(value);
	}

	item2->Release();
	return result;
}

// A drive letter parses as a one-letter scheme, so a scheme is two or more.
bool isUrl(const QString& target) {
	return !target.startsWith(QStringLiteral("::")) && QUrl(target).scheme().size() > 1;
}

QString shortcutTarget(IShellLinkW* link, const QString& path) {
	IPersistFile* file = nullptr;
	if (FAILED(link->QueryInterface(IID_PPV_ARGS(&file)))) return QString();

	auto result = QString();
	if (SUCCEEDED(file->Load(reinterpret_cast<LPCOLESTR>(path.utf16()), STGM_READ))) { // NOLINT
		auto buffer = std::array<wchar_t, MAX_PATH> {};
		if (link->GetPath(buffer.data(), static_cast<int>(buffer.size()), nullptr, 0) == S_OK) {
			result = QString::fromWCharArray(buffer.data());
		}
	}

	file->Release();
	return result;
}

// The Start menu's shortcuts by target, lowercased, to their file names. The
// Apps folder shows an app under the name in the display language ("工作管理員"),
// and the file under it is often the English one ("Task Manager.lnk").
QHash<QString, QString> shortcutNames() {
	auto names = QHash<QString, QString>();

	IShellLinkW* link = nullptr;
	if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&link)))) {
		return names;
	}

	for (const auto& root: DesktopEntryManager::desktopPaths()) {
		auto it = QDirIterator(root, {QStringLiteral("*.lnk")}, QDir::Files, QDirIterator::Subdirectories);
		while (it.hasNext()) {
			auto path = QDir::toNativeSeparators(it.next());
			auto target = shortcutTarget(link, path);
			if (!target.isEmpty()) names.insert(target.toLower(), it.fileInfo().completeBaseName());
		}
	}

	link->Release();
	return names;
}

bool isPage(const QString& url) {
	static const auto schemes = QStringList {
	    QStringLiteral("http"),
	    QStringLiteral("https"),
	    QStringLiteral("ftp"),
	    QStringLiteral("file"),
	    QStringLiteral("mailto")
	};
	return schemes.contains(QUrl(url).scheme().toLower());
}

// The id is the item's AppUserModelID (packaged apps, and programs that set
// one), or else its target: a program's path under a known folder GUID, or a
// URL. The target (System.Link.TargetParsingPath) is empty for packaged apps
// and `::{CLSID}` for shell items such as File Explorer.
// Every kind runs through `shell:AppsFolder\<id>`, as the Start menu does,
// except URLs, which Explorer hands to their scheme's handler directly.
// startupClass is the target's file name without .exe, which is what window
// managers such as komorebi report as an app id, so heuristicLookup finds it.
// Shortcuts to anything but an app (readmes, help, web pages) and uninstallers
// are NoDisplay: byId still finds them. A shortcut's file name, where it isn't
// the name already, is a keyword.
ParsedDesktopEntryData parseItem(IShellItem* item, const QHash<QString, QString>& shortcutNames) {
	auto data = ParsedDesktopEntryData();
	data.id = displayName(item, SIGDN_PARENTRELATIVEPARSING);
	data.name = displayName(item, SIGDN_NORMALDISPLAY);
	auto target = stringProperty(item, PKEY_Link_TargetParsingPath);
	data.execString = target.isEmpty() ? data.id : target;
	data.command = {
	    QStringLiteral("explorer.exe"),
	    isUrl(target) ? target : QStringLiteral("shell:AppsFolder\\") + data.id
	};

	auto info = QFileInfo(target);
	auto isProgram = info.suffix().compare(QStringLiteral("exe"), Qt::CaseInsensitive) == 0;
	if (isProgram) data.startupClass = info.completeBaseName();

	if (auto it = shortcutNames.find(target.toLower()); !target.isEmpty() && it != shortcutNames.end()) {
		if (it->compare(data.name, Qt::CaseInsensitive) != 0) data.keywords.append(*it);
	}

	auto isApp = target.isEmpty() || target.startsWith(QStringLiteral("::")) || isProgram
	          || (isUrl(target) && !isPage(target));
	if (!isApp) data.noDisplay = true;

	// MSI's own uninstall entries run msiexec; the rest are uninstall.exe or
	// Inno Setup's unins000.exe, under names in any language.
	auto program = data.startupClass.toLower();
	if (program == QStringLiteral("msiexec") || program.startsWith(QStringLiteral("unins"))
	    || data.name.contains(QStringLiteral("uninstall"), Qt::CaseInsensitive))
	{
		data.noDisplay = true;
	}

	return data;
}

} // namespace

QList<ParsedDesktopEntryData> DesktopEntry::scanAppsFolder() {
	auto entries = QList<ParsedDesktopEntryData>();
	ComScope com;
	auto names = shortcutNames();

	IShellItem* folder = nullptr;
	if (FAILED(SHGetKnownFolderItem(FOLDERID_AppsFolder, KF_FLAG_DEFAULT, nullptr, IID_PPV_ARGS(&folder))))
	{
		qWarning() << "Could not open the Apps folder";
		return entries;
	}

	IEnumShellItems* items = nullptr;
	if (SUCCEEDED(folder->BindToHandler(nullptr, BHID_EnumItems, IID_PPV_ARGS(&items)))) {
		IShellItem* item = nullptr;
		while (items->Next(1, &item, nullptr) == S_OK) {
			auto data = parseItem(item, names);
			if (!data.id.isEmpty()) entries.append(data);
			item->Release();
		}
		items->Release();
	} else {
		qWarning() << "Could not list the Apps folder";
	}

	folder->Release();
	return entries;
}
