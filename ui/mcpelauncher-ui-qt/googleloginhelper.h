#ifndef GOOGLELOGINHELPER_H
#define GOOGLELOGINHELPER_H

#include <QObject>
#include <QSettings>
#include <playapi/login.h>
#include <playapi/device_info.h>
#include "encrypted_file_login_cache.h"
#include "googleaccount.h"

class QWindow;
class GoogleLoginWindow;

class GoogleLoginHelper : public QObject {
    Q_OBJECT
    Q_PROPERTY(GoogleAccount* account READ account NOTIFY accountInfoChanged)
    Q_PROPERTY(bool includeIncompatible READ getIncludeIncompatible WRITE setIncludeIncompatible)
    Q_PROPERTY(QString singleArch READ getSingleArch WRITE setSingleArch)
    Q_PROPERTY(bool hideLatest READ hideLatest NOTIFY accountInfoChanged)
    Q_PROPERTY(bool hasEncryptedCredentials READ getHasEncryptedCredentials NOTIFY accountInfoChanged)
    Q_PROPERTY(QString unlockkey READ getUnlockkey WRITE setUnlockkey NOTIFY accountInfoChanged)
    Q_PROPERTY(bool chromeOS MEMBER chromeOS WRITE setChromeOS)

private:
    QSettings settings;
    GoogleLoginWindow* window = nullptr;
    GoogleAccount currentAccount;
    playapi::device_info device;
    playapi::encrypted_file_login_cache loginCache;
    playapi::login_api login;
    bool hasAccount = false;
    bool includeIncompatible = false;
    bool hasEncryptedCredentials = false;
    QString singleArch;
    QString unlockkey;
    bool chromeOS = false;

    static std::string getTokenCachePath();

    void loadDeviceState();
    void saveDeviceState();

    void onLoginFinished(int code);

    bool getIncludeIncompatible() {
        return includeIncompatible;
    }

    QString getSingleArch() {
        return singleArch;
    }
    QString getUnlockkey() {
        return unlockkey;
    }

    void loadAccount();

    void updateDevice();

    void setIncludeIncompatible(bool includeIncompatible) {
        if (this->includeIncompatible != includeIncompatible) {
            this->includeIncompatible = includeIncompatible;
            updateDevice();
        }
    }

    void setSingleArch(QString singleArch) {
        if (this->singleArch != singleArch) {
            this->singleArch = singleArch;
            updateDevice();
        }
    }

    void setUnlockkey(QString key) {
        if (this->unlockkey != key) {
            this->unlockkey = key;
            loadAccount();
        }
    }

public:
    bool getHasEncryptedCredentials() {
        return hasEncryptedCredentials;
    }
    void setChromeOS(bool isChromeOS) {
        auto shouldUpdateDevice = chromeOS != isChromeOS;
        chromeOS = isChromeOS;
        if(shouldUpdateDevice) {
            updateDevice();
        }
    }
    bool isChromeOS() {
        return chromeOS;
    }
    GoogleLoginHelper();

    ~GoogleLoginHelper();

    GoogleAccount* account() {
        return hasAccount ? &currentAccount : nullptr;
    }

    playapi::device_info& getDevice() { return device; }
    playapi::login_api& getLoginApi() { return login; }

    bool hideLatest();
public slots:
    void acquireAccount(QWindow *parent);

    void signOut();

    QStringList getAbis(bool includeIncompatible);

    QString GetSupportReport();

    bool isSupported();
signals:
    void accountAcquireFinished(GoogleAccount* account);

    void accountInfoChanged();

    void loginError(QString error);
};

#endif // GOOGLELOGINHELPER_H
