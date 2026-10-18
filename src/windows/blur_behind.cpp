#include "blur_behind.hpp"

#include <qnamespace.h>
#include <qobject.h>
#include <qquickwindow.h>
#include <qrect.h>
#include <qregion.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../core/region.hpp"
#include "../window/proxywindow.hpp"
#include "../window/windowinterface.hpp"
#include "backdrop.hpp"

namespace qs::windows {

namespace {

// Scales each edge rather than each rect, so touching rects still touch.
QRegion scaleRegion(const QRegion& region, qreal scale) {
	QRegion scaled;

	for (const auto& rect: region) {
		auto left = qRound(rect.left() * scale);
		auto top = qRound(rect.top() * scale);
		auto right = qRound((rect.right() + 1) * scale);
		auto bottom = qRound((rect.bottom() + 1) * scale);
		scaled += QRect(left, top, right - left, bottom - top);
	}

	return scaled;
}

} // namespace

BlurBehind* BlurBehind::qmlAttachedProperties(QObject* object) {
	auto* proxyWindow = qobject_cast<ProxyWindowBase*>(object);

	if (!proxyWindow) {
		if (auto* iface = qobject_cast<WindowInterface*>(object)) {
			proxyWindow = iface->proxyWindow();
		}
	}

	if (!proxyWindow) return nullptr;
	return new BlurBehind(proxyWindow);
}

BlurBehind::BlurBehind(ProxyWindowBase* window): QObject(window), proxyWindow(window) {
	QObject::connect(window, &ProxyWindowBase::windowConnected, this, &BlurBehind::onWindowConnected);
	QObject::connect(window, &ProxyWindowBase::polished, this, &BlurBehind::onWindowPolished);
	QObject::connect(window, &ProxyWindowBase::widthChanged, this, &BlurBehind::updateRegion);
	QObject::connect(window, &ProxyWindowBase::heightChanged, this, &BlurBehind::updateRegion);

	QObject::connect(
	    window,
	    &ProxyWindowBase::devicePixelRatioChanged,
	    this,
	    &BlurBehind::updateRegion
	);

	// Sent before the window's children, the region among them, are deleted.
	QObject::connect(window, &QObject::destroyed, this, &BlurBehind::onProxyWindowDestroyed);

	if (window->backingWindow()) {
		this->onWindowConnected();
	}
}

BlurBehind::~BlurBehind() {
	if (!this->mWindow) return;

	if (auto* backdrop = this->mWindow->findChild<BlurBackdrop*>(Qt::FindDirectChildrenOnly)) {
		backdrop->release(this);
	}
}

void BlurBehind::setRegion(PendingRegion* region) {
	if (region == this->mRegion) return;

	if (this->mRegion) {
		QObject::disconnect(this->mRegion, nullptr, this, nullptr);
	}

	this->mRegion = region;

	if (region) {
		QObject::connect(region, &QObject::destroyed, this, &BlurBehind::onRegionDestroyed);
		QObject::connect(region, &PendingRegion::changed, this, &BlurBehind::updateRegion);
	}

	this->updateRegion();
	emit this->regionChanged();
}

void BlurBehind::setRadius(qreal radius) {
	if (qFuzzyCompare(radius, this->mRadius)) return;
	this->mRadius = radius;
	this->updateRegion();
	emit this->radiusChanged();
}

void BlurBehind::setSaturation(qreal saturation) {
	if (qFuzzyCompare(saturation, this->mSaturation)) return;
	this->mSaturation = saturation;
	this->updateRegion();
	emit this->saturationChanged();
}

void BlurBehind::setXray(bool xray) {
	if (xray == this->mXray) return;
	this->mXray = xray;
	this->updateRegion();
	emit this->xrayChanged();
}

void BlurBehind::onRegionDestroyed() {
	this->mRegion = nullptr;
	this->updateRegion();
	emit this->regionChanged();
}

void BlurBehind::onProxyWindowDestroyed() { this->proxyWindow = nullptr; }

void BlurBehind::updateRegion() {
	if (!this->proxyWindow || !this->mWindow) return;

	this->pendingRegion = true;
	this->proxyWindow->schedulePolish();
}

void BlurBehind::onWindowPolished() {
	if (!this->pendingRegion || !this->mWindow) return;
	this->pendingRegion = false;
	auto* backdrop = this->mWindow->findChild<BlurBackdrop*>(Qt::FindDirectChildrenOnly);
	if (!this->mRegion && !backdrop) return;

	auto scale = this->mWindow->devicePixelRatio();

	QRegion region;
	if (this->mRegion) {
		region = this->mRegion->applyTo(QRect(0, 0, this->mWindow->width(), this->mWindow->height()));
		region = scaleRegion(region, scale);
	}

	if (!backdrop) backdrop = BlurBackdrop::forWindow(this->mWindow);
	backdrop->claim(this);
	backdrop->setRadius(this->mRadius * scale);
	backdrop->setSaturation(this->mSaturation);
	backdrop->setXray(this->mXray);
	backdrop->setRegion(region);
}

void BlurBehind::onWindowConnected() {
	this->mWindow = this->proxyWindow->backingWindow();
	this->updateRegion();
}

} // namespace qs::windows
