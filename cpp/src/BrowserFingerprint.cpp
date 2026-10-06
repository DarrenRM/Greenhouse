#include "../include/BrowserFingerprint.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QRegularExpression>
#include <QSet>

#include <UIAutomation.h>
#include <OleAuto.h>
#include <algorithm>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace {

class ScopedComInitializer {
public:
    ScopedComInitializer()
    {
        m_result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        m_shouldUninitialize = m_result == S_OK || m_result == S_FALSE;
    }

    ~ScopedComInitializer()
    {
        if (m_shouldUninitialize) {
            CoUninitialize();
        }
    }

    bool isUsable() const
    {
        return SUCCEEDED(m_result) || m_result == RPC_E_CHANGED_MODE;
    }

private:
    HRESULT m_result = E_FAIL;
    bool m_shouldUninitialize = false;
};

QString bstrToQString(BSTR value)
{
    if (!value) {
        return QString();
    }

    QString result = QString::fromWCharArray(value);
    SysFreeString(value);
    return result;
}

QStringList uniqueSorted(const QStringList& values)
{
    QSet<QString> seen;
    QStringList unique;
    for (const QString& value : values) {
        if (value.isEmpty() || seen.contains(value)) {
            continue;
        }
        seen.insert(value);
        unique.append(value);
    }
    std::sort(unique.begin(), unique.end());
    return unique;
}

bool isPinnedTabTitle(const QString& title)
{
    const QString normalized = title.toLower().trimmed();
    return normalized.endsWith(QStringLiteral(" - pinned")) ||
           normalized.contains(QStringLiteral(" pinned"));
}

QString removeBrowserSuffix(QString title)
{
    static const QRegularExpression browserSuffix(
        QStringLiteral("\\s+-\\s+(brave|google chrome|chrome|microsoft edge|edge|firefox|opera|vivaldi)$"),
        QRegularExpression::CaseInsensitiveOption);
    title.remove(browserSuffix);
    return title;
}

} // namespace

namespace BrowserFingerprinting {

QString browserFamilyForProcess(const QString& processName)
{
    const QString lower = processName.toLower();
    if (lower == QLatin1String("brave.exe")) {
        return QStringLiteral("brave");
    }
    if (lower == QLatin1String("chrome.exe")) {
        return QStringLiteral("chrome");
    }
    if (lower == QLatin1String("msedge.exe")) {
        return QStringLiteral("edge");
    }
    if (lower == QLatin1String("firefox.exe")) {
        return QStringLiteral("firefox");
    }
    if (lower == QLatin1String("opera.exe")) {
        return QStringLiteral("opera");
    }
    if (lower == QLatin1String("vivaldi.exe")) {
        return QStringLiteral("vivaldi");
    }
    return QString();
}

bool isSupportedBrowser(const WindowInfo& info)
{
    return !browserFamilyForProcess(info.processName).isEmpty();
}

QString normalizeTitle(QString title)
{
    title = title.trimmed().toLower();
    title.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    title = removeBrowserSuffix(title);

    static const QRegularExpression pinnedSuffix(
        QStringLiteral("\\s+-\\s+pinned$"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression inactiveSuffix(
        QStringLiteral("\\s+-\\s+inactive tab\\s+-\\s+.*$"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression memorySuffix(
        QStringLiteral("\\s+-\\s+memory usage\\s+-\\s+.*$"),
        QRegularExpression::CaseInsensitiveOption);

    title.remove(pinnedSuffix);
    title.remove(inactiveSuffix);
    title.remove(memorySuffix);
    title.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return title.trimmed();
}

QString hashNormalizedTitle(const QString& normalizedTitle)
{
    if (normalizedTitle.isEmpty()) {
        return QString();
    }

    const QByteArray hash = QCryptographicHash::hash(normalizedTitle.toUtf8(),
                                                     QCryptographicHash::Sha256).toHex();
    return QString::fromLatin1(hash.left(24));
}

BrowserFingerprint fromTabTitles(const WindowInfo& info,
                                  const QStringList& tabTitles,
                                  const QString& selectedTabTitle)
{
    BrowserFingerprint fingerprint;
    fingerprint.browserFamily = browserFamilyForProcess(info.processName);
    if (fingerprint.browserFamily.isEmpty()) {
        return fingerprint;
    }

    QStringList tabHashes;
    QStringList pinnedHashes;
    for (const QString& rawTitle : tabTitles) {
        const QString normalized = normalizeTitle(rawTitle);
        const QString hash = hashNormalizedTitle(normalized);
        if (hash.isEmpty()) {
            continue;
        }

        tabHashes.append(hash);
        ++fingerprint.tabCount;
        if (isPinnedTabTitle(rawTitle)) {
            pinnedHashes.append(hash);
            ++fingerprint.pinnedTabCount;
        }
    }

    const QString activeTitle = selectedTabTitle.isEmpty() ? info.title : selectedTabTitle;
    fingerprint.activeTitleHash = hashNormalizedTitle(normalizeTitle(activeTitle));
    fingerprint.tabTitleHashes = uniqueSorted(tabHashes);
    fingerprint.pinnedTabTitleHashes = uniqueSorted(pinnedHashes);
    fingerprint.isValid = !fingerprint.browserFamily.isEmpty() &&
                          !fingerprint.tabTitleHashes.isEmpty();
    return fingerprint;
}

BrowserFingerprint collectForWindow(HWND hwnd, const WindowInfo& info)
{
    if (!hwnd || !IsWindow(hwnd) || !isSupportedBrowser(info)) {
        return BrowserFingerprint();
    }

    ScopedComInitializer com;
    if (!com.isUsable()) {
        qWarning() << "Browser fingerprint skipped because COM is unavailable for HWND:" << hwnd;
        return BrowserFingerprint();
    }

    ComPtr<IUIAutomation> automation;
    HRESULT hr = CoCreateInstance(CLSID_CUIAutomation,
                                  nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&automation));
    if (FAILED(hr) || !automation) {
        qWarning() << "Browser fingerprint skipped because UI Automation could not be created for HWND:"
                   << hwnd << "hr:" << QString::number(static_cast<qulonglong>(hr), 16);
        return BrowserFingerprint();
    }

    ComPtr<IUIAutomationElement> root;
    hr = automation->ElementFromHandle(hwnd, &root);
    if (FAILED(hr) || !root) {
        qWarning() << "Browser fingerprint skipped because UI Automation could not inspect HWND:"
                   << hwnd << "hr:" << QString::number(static_cast<qulonglong>(hr), 16);
        return BrowserFingerprint();
    }

    VARIANT tabType;
    VariantInit(&tabType);
    tabType.vt = VT_I4;
    tabType.lVal = UIA_TabItemControlTypeId;

    ComPtr<IUIAutomationCondition> tabCondition;
    hr = automation->CreatePropertyCondition(UIA_ControlTypePropertyId, tabType, &tabCondition);
    VariantClear(&tabType);
    if (FAILED(hr) || !tabCondition) {
        return BrowserFingerprint();
    }

    ComPtr<IUIAutomationElementArray> tabElements;
    hr = root->FindAll(TreeScope_Descendants, tabCondition.Get(), &tabElements);
    if (FAILED(hr) || !tabElements) {
        qWarning() << "Browser fingerprint skipped because no tab collection was available for HWND:"
                   << hwnd << "hr:" << QString::number(static_cast<qulonglong>(hr), 16);
        return BrowserFingerprint();
    }

    int length = 0;
    if (FAILED(tabElements->get_Length(&length)) || length <= 0) {
        return BrowserFingerprint();
    }

    QStringList tabTitles;
    QString selectedTitle;
    const int maxTabsToInspect = length < 200 ? length : 200;
    for (int i = 0; i < maxTabsToInspect; ++i) {
        ComPtr<IUIAutomationElement> tabElement;
        if (FAILED(tabElements->GetElement(i, &tabElement)) || !tabElement) {
            continue;
        }

        BSTR rawName = nullptr;
        QString name;
        if (SUCCEEDED(tabElement->get_CurrentName(&rawName))) {
            name = bstrToQString(rawName).trimmed();
        }
        if (name.isEmpty()) {
            continue;
        }

        tabTitles.append(name);

        VARIANT selected;
        VariantInit(&selected);
        if (SUCCEEDED(tabElement->GetCurrentPropertyValue(UIA_SelectionItemIsSelectedPropertyId, &selected)) &&
            selected.vt == VT_BOOL &&
            selected.boolVal == VARIANT_TRUE) {
            selectedTitle = name;
        }
        VariantClear(&selected);
    }

    BrowserFingerprint fingerprint = fromTabTitles(info, tabTitles, selectedTitle);
    if (fingerprint.isValid) {
        qInfo() << "Collected browser fingerprint for HWND:" << hwnd
                << "family:" << fingerprint.browserFamily
                << "tabs:" << fingerprint.tabCount
                << "pinned:" << fingerprint.pinnedTabCount;
    } else {
        qInfo() << "Browser fingerprint had no usable tab signals for HWND:" << hwnd
                << "tab elements:" << length;
    }
    return fingerprint;
}

} // namespace BrowserFingerprinting
