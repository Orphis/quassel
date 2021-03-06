/***************************************************************************
 *   Copyright (C) 2005-2013 by the Quassel Project                        *
 *   devel@quassel-irc.org                                                 *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) version 3.                                           *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.         *
 ***************************************************************************/

#include "quassel.h"

#include <iostream>
#include <signal.h>
#if !defined Q_OS_WIN32 && !defined Q_OS_MAC
#  include <sys/resource.h>
#endif

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QSettings>
#include <QTranslator>
#include <QHostAddress>

#include "message.h"
#include "identity.h"
#include "network.h"
#include "bufferinfo.h"
#include "types.h"
#include "syncableobject.h"
#include "logger.h"

Quassel::BuildInfo Quassel::_buildInfo;
AbstractCliParser *Quassel::_cliParser = 0;
Quassel::RunMode Quassel::_runMode;
QString Quassel::_configDirPath;
QString Quassel::_translationDirPath;
QStringList Quassel::_dataDirPaths;
bool Quassel::_initialized = false;
bool Quassel::DEBUG = false;
QString Quassel::_coreDumpFileName;
Quassel *Quassel::_instance = 0;
bool Quassel::_handleCrashes = true;
Quassel::LogLevel Quassel::_logLevel = InfoLevel;
QFile *Quassel::_logFile = 0;
bool Quassel::_logToSyslog = false;

Quassel::Quassel()
{
    Q_ASSERT(!_instance);
    _instance = this;

    // We catch SIGTERM and SIGINT (caused by Ctrl+C) to graceful shutdown Quassel.
    signal(SIGTERM, handleSignal);
    signal(SIGINT, handleSignal);
}


Quassel::~Quassel()
{
    if (logFile()) {
        logFile()->close();
        logFile()->deleteLater();
    }
    delete _cliParser;
}


bool Quassel::init()
{
    if (_initialized)
        return true;  // allow multiple invocations because of MonolithicApplication

    if (_handleCrashes) {
        // we have crashhandler for win32 and unix (based on execinfo).
#if defined(Q_OS_WIN32) || defined(HAVE_EXECINFO)
# ifndef Q_OS_WIN32
        // we only handle crashes ourselves if coredumps are disabled
        struct rlimit *limit = (rlimit *)malloc(sizeof(struct rlimit));
        int rc = getrlimit(RLIMIT_CORE, limit);

        if (rc == -1 || !((long)limit->rlim_cur > 0 || limit->rlim_cur == RLIM_INFINITY)) {
# endif /* Q_OS_WIN32 */
        signal(SIGABRT, handleSignal);
        signal(SIGSEGV, handleSignal);
# ifndef Q_OS_WIN32
        signal(SIGBUS, handleSignal);
    }
    free(limit);
# endif /* Q_OS_WIN32 */
#endif /* Q_OS_WIN32 || HAVE_EXECINFO */
    }

    _initialized = true;
    qsrand(QTime(0, 0, 0).secsTo(QTime::currentTime()));

    registerMetaTypes();

    Network::setDefaultCodecForServer("ISO-8859-1");
    Network::setDefaultCodecForEncoding("UTF-8");
    Network::setDefaultCodecForDecoding("ISO-8859-15");

    if (isOptionSet("help")) {
        cliParser()->usage();
        return false;
    }

    if (isOptionSet("version")) {
        std::cout << qPrintable("Quassel IRC: " + Quassel::buildInfo().plainVersionString) << std::endl;
        return false;
    }

    DEBUG = isOptionSet("debug");

    // set up logging
    if (Quassel::runMode() != Quassel::ClientOnly) {
        if (isOptionSet("loglevel")) {
            QString level = optionValue("loglevel");

            if (level == "Debug") _logLevel = DebugLevel;
            else if (level == "Info") _logLevel = InfoLevel;
            else if (level == "Warning") _logLevel = WarningLevel;
            else if (level == "Error") _logLevel = ErrorLevel;
        }

        QString logfilename = optionValue("logfile");
        if (!logfilename.isEmpty()) {
            _logFile = new QFile(logfilename);
            if (!_logFile->open(QIODevice::Append | QIODevice::Text)) {
                qWarning() << "Could not open log file" << logfilename << ":" << _logFile->errorString();
                _logFile->deleteLater();
                _logFile = 0;
            }
        }
#ifdef HAVE_SYSLOG
        _logToSyslog = isOptionSet("syslog");
#endif
    }

    return true;
}


void Quassel::quit()
{
    QCoreApplication::quit();
}


//! Register our custom types with Qt's Meta Object System.
/**  This makes them available for QVariant and in signals/slots, among other things.
*
*/
void Quassel::registerMetaTypes()
{
    // Complex types
    qRegisterMetaType<Message>("Message");
    qRegisterMetaType<BufferInfo>("BufferInfo");
    qRegisterMetaType<NetworkInfo>("NetworkInfo");
    qRegisterMetaType<Network::Server>("Network::Server");
    qRegisterMetaType<Identity>("Identity");

    qRegisterMetaTypeStreamOperators<Message>("Message");
    qRegisterMetaTypeStreamOperators<BufferInfo>("BufferInfo");
    qRegisterMetaTypeStreamOperators<NetworkInfo>("NetworkInfo");
    qRegisterMetaTypeStreamOperators<Network::Server>("Network::Server");
    qRegisterMetaTypeStreamOperators<Identity>("Identity");

    qRegisterMetaType<IdentityId>("IdentityId");
    qRegisterMetaType<BufferId>("BufferId");
    qRegisterMetaType<NetworkId>("NetworkId");
    qRegisterMetaType<UserId>("UserId");
    qRegisterMetaType<AccountId>("AccountId");
    qRegisterMetaType<MsgId>("MsgId");

    qRegisterMetaType<QHostAddress>("QHostAddress");

    qRegisterMetaTypeStreamOperators<IdentityId>("IdentityId");
    qRegisterMetaTypeStreamOperators<BufferId>("BufferId");
    qRegisterMetaTypeStreamOperators<NetworkId>("NetworkId");
    qRegisterMetaTypeStreamOperators<UserId>("UserId");
    qRegisterMetaTypeStreamOperators<AccountId>("AccountId");
    qRegisterMetaTypeStreamOperators<MsgId>("MsgId");

    // Versions of Qt prior to 4.7 didn't define QVariant as a meta type
    if (!QMetaType::type("QVariant")) {
        qRegisterMetaType<QVariant>("QVariant");
        qRegisterMetaTypeStreamOperators<QVariant>("QVariant");
    }
}


void Quassel::setupBuildInfo(const QString &generated)
{
    _buildInfo.applicationName = "Quassel IRC";
    _buildInfo.coreApplicationName = "quasselcore";
    _buildInfo.clientApplicationName = "quasselclient";
    _buildInfo.organizationName = "Quassel Project";
    _buildInfo.organizationDomain = "quassel-irc.org";

    QStringList gen = generated.split(',');
    Q_ASSERT(gen.count() == 10);
    _buildInfo.baseVersion = gen[0];
    _buildInfo.generatedVersion = gen[1];
    _buildInfo.isSourceDirty = !gen[2].isEmpty();
    _buildInfo.commitHash = gen[3];
    _buildInfo.commitDate = gen[4].toUInt();
    _buildInfo.protocolVersion = gen[5].toUInt();
    _buildInfo.clientNeedsProtocol = gen[6].toUInt();
    _buildInfo.coreNeedsProtocol = gen[7].toUInt();
    _buildInfo.buildDate = QString("%1 %2").arg(gen[8], gen[9]);
    // create a nice version string
    if (_buildInfo.generatedVersion.isEmpty()) {
        if (!_buildInfo.commitHash.isEmpty()) {
            // dist version
            _buildInfo.plainVersionString = QString("v%1 (dist-%2)")
                                            .arg(_buildInfo.baseVersion)
                                            .arg(_buildInfo.commitHash.left(7));
            _buildInfo.fancyVersionString = QString("v%1 (dist-<a href=\"http://git.quassel-irc.org/?p=quassel.git;a=commit;h=%3\">%2</a>)")
                                            .arg(_buildInfo.baseVersion)
                                            .arg(_buildInfo.commitHash.left(7))
                                            .arg(_buildInfo.commitHash);
        }
        else {
            // we only have a base version :(
            _buildInfo.plainVersionString = QString("v%1 (unknown rev)").arg(_buildInfo.baseVersion);
        }
    }
    else {
        // analyze what we got from git-describe
        QRegExp rx("(.*)-(\\d+)-g([0-9a-f]+)$");
        if (rx.exactMatch(_buildInfo.generatedVersion)) {
            QString distance = rx.cap(2) == "0" ? QString() : QString("%1+%2 ").arg(rx.cap(1), rx.cap(2));
            _buildInfo.plainVersionString = QString("v%1 (%2git-%3%4)")
                                            .arg(_buildInfo.baseVersion, distance, rx.cap(3))
                                            .arg(_buildInfo.isSourceDirty ? "*" : "");
            if (!_buildInfo.commitHash.isEmpty()) {
                _buildInfo.fancyVersionString = QString("v%1 (%2git-<a href=\"http://git.quassel-irc.org/?p=quassel.git;a=commit;h=%5\">%3</a>%4)")
                                                .arg(_buildInfo.baseVersion, distance, rx.cap(3))
                                                .arg(_buildInfo.isSourceDirty ? "*" : "")
                                                .arg(_buildInfo.commitHash);
            }
        }
        else {
            _buildInfo.plainVersionString = QString("v%1 (invalid rev)").arg(_buildInfo.baseVersion);
        }
    }
    if (_buildInfo.fancyVersionString.isEmpty())
        _buildInfo.fancyVersionString = _buildInfo.plainVersionString;
}


//! Signal handler for graceful shutdown.
void Quassel::handleSignal(int sig)
{
    switch (sig) {
    case SIGTERM:
    case SIGINT:
        qWarning("%s", qPrintable(QString("Caught signal %1 - exiting.").arg(sig)));
        if (_instance)
            _instance->quit();
        else
            QCoreApplication::quit();
        break;
    case SIGABRT:
    case SIGSEGV:
#ifndef Q_OS_WIN32
    case SIGBUS:
#endif
        logBacktrace(coreDumpFileName());
        exit(EXIT_FAILURE);
        break;
    default:
        break;
    }
}


void Quassel::logFatalMessage(const char *msg)
{
#ifdef Q_OS_MAC
    Q_UNUSED(msg)
#else
    QFile dumpFile(coreDumpFileName());
    dumpFile.open(QIODevice::Append);
    QTextStream dumpStream(&dumpFile);

    dumpStream << "Fatal: " << msg << '\n';
    dumpStream.flush();
    dumpFile.close();
#endif
}


Quassel::Features Quassel::features()
{
    Features feats = 0;
    for (int i = 1; i <= NumFeatures; i <<= 1)
        feats |= (Feature)i;

    return feats;
}


const QString &Quassel::coreDumpFileName()
{
    if (_coreDumpFileName.isEmpty()) {
        QDir configDir(configDirPath());
        _coreDumpFileName = configDir.absoluteFilePath(QString("Quassel-Crash-%1.log").arg(QDateTime::currentDateTime().toString("yyyyMMdd-hhmm")));
        QFile dumpFile(_coreDumpFileName);
        dumpFile.open(QIODevice::Append);
        QTextStream dumpStream(&dumpFile);
        dumpStream << "Quassel IRC: " << _buildInfo.baseVersion << ' ' << _buildInfo.commitHash << '\n';
        qDebug() << "Quassel IRC: " << _buildInfo.baseVersion << ' ' << _buildInfo.commitHash;
        dumpStream.flush();
        dumpFile.close();
    }
    return _coreDumpFileName;
}


QString Quassel::configDirPath()
{
    if (!_configDirPath.isEmpty())
        return _configDirPath;

    if (Quassel::isOptionSet("datadir")) {
        qWarning() << "Obsolete option --datadir used!";
        _configDirPath = Quassel::optionValue("datadir");
    }
    else if (Quassel::isOptionSet("configdir")) {
        _configDirPath = Quassel::optionValue("configdir");
    }
    else {
#ifdef Q_WS_MAC
        // On Mac, the path is always the same
        _configDirPath = QDir::homePath() + "/Library/Application Support/Quassel/";
#else
        // We abuse QSettings to find us a sensible path on the other platforms
#  ifdef Q_WS_WIN
        // don't use the registry
        QSettings::Format format = QSettings::IniFormat;
#  else
        QSettings::Format format = QSettings::NativeFormat;
#  endif
        QSettings s(format, QSettings::UserScope, QCoreApplication::organizationDomain(), buildInfo().applicationName);
        QFileInfo fileInfo(s.fileName());
        _configDirPath = fileInfo.dir().absolutePath();
#endif /* Q_WS_MAC */
    }

    if (!_configDirPath.endsWith(QDir::separator()) && !_configDirPath.endsWith('/'))
        _configDirPath += QDir::separator();

    QDir qDir(_configDirPath);
    if (!qDir.exists(_configDirPath)) {
        if (!qDir.mkpath(_configDirPath)) {
            qCritical() << "Unable to create Quassel config directory:" << qPrintable(qDir.absolutePath());
            return QString();
        }
    }

    return _configDirPath;
}


QStringList Quassel::dataDirPaths()
{
    return _dataDirPaths;
}


QStringList Quassel::findDataDirPaths() const
{
    QStringList dataDirNames = QString(qgetenv("XDG_DATA_DIRS")).split(':', QString::SkipEmptyParts);

    if (!dataDirNames.isEmpty()) {
        for (int i = 0; i < dataDirNames.count(); i++)
            dataDirNames[i].append("/apps/quassel/");
    }
    else {
        // Provide a fallback
#ifdef Q_OS_WIN32
        dataDirNames << qgetenv("APPDATA") + QCoreApplication::organizationDomain() + "/share/apps/quassel/"
                     << qgetenv("APPDATA") + QCoreApplication::organizationDomain()
                     << QCoreApplication::applicationDirPath();
    }
#elif defined Q_WS_MAC
        dataDirNames << QDir::homePath() + "/Library/Application Support/Quassel/"
                     << QCoreApplication::applicationDirPath();
    }
#else
        dataDirNames.append("/usr/share/apps/quassel/");
    }
    // on UNIX, we always check our install prefix
    QString appDir = QCoreApplication::applicationDirPath();
    int binpos = appDir.lastIndexOf("/bin");
    if (binpos >= 0) {
        appDir.replace(binpos, 4, "/share");
        appDir.append("/apps/quassel/");
        if (!dataDirNames.contains(appDir))
            dataDirNames.append(appDir);
    }
#endif

    // add resource path and workdir just in case
    dataDirNames << QCoreApplication::applicationDirPath() + "/data/"
                 << ":/data/";

    // append trailing '/' and check for existence
    QStringList::Iterator iter = dataDirNames.begin();
    while (iter != dataDirNames.end()) {
        if (!iter->endsWith(QDir::separator()) && !iter->endsWith('/'))
            iter->append(QDir::separator());
        if (!QFile::exists(*iter))
            iter = dataDirNames.erase(iter);
        else
            ++iter;
    }

    return dataDirNames;
}


QString Quassel::findDataFilePath(const QString &fileName)
{
    QStringList dataDirs = dataDirPaths();
    foreach(QString dataDir, dataDirs) {
        QString path = dataDir + fileName;
        if (QFile::exists(path))
            return path;
    }
    return QString();
}


QStringList Quassel::scriptDirPaths()
{
    QStringList res(configDirPath() + "scripts/");
    foreach(QString path, dataDirPaths())
    res << path + "scripts/";
    return res;
}


QString Quassel::translationDirPath()
{
    if (_translationDirPath.isEmpty()) {
        // We support only one translation dir; fallback mechanisms wouldn't work else.
        // This means that if we have a $data/translations dir, the internal :/i18n resource won't be considered.
        foreach(const QString &dir, dataDirPaths()) {
            if (QFile::exists(dir + "translations/")) {
                _translationDirPath = dir + "translations/";
                break;
            }
        }
        if (_translationDirPath.isEmpty())
            _translationDirPath = ":/i18n/";
    }
    return _translationDirPath;
}


void Quassel::loadTranslation(const QLocale &locale)
{
    QTranslator *qtTranslator = QCoreApplication::instance()->findChild<QTranslator *>("QtTr");
    QTranslator *quasselTranslator = QCoreApplication::instance()->findChild<QTranslator *>("QuasselTr");

    if (qtTranslator)
        qApp->removeTranslator(qtTranslator);
    if (quasselTranslator)
        qApp->removeTranslator(quasselTranslator);

    // We use QLocale::C to indicate that we don't want a translation
    if (locale.language() == QLocale::C)
        return;

    qtTranslator = new QTranslator(qApp);
    qtTranslator->setObjectName("QtTr");
    qApp->installTranslator(qtTranslator);

    quasselTranslator = new QTranslator(qApp);
    quasselTranslator->setObjectName("QuasselTr");
    qApp->installTranslator(quasselTranslator);

#if QT_VERSION >= 0x040800 && !defined Q_OS_MAC
    bool success = qtTranslator->load(locale, QString("qt_"), translationDirPath());
    if (!success)
        qtTranslator->load(locale, QString("qt_"), QLibraryInfo::location(QLibraryInfo::TranslationsPath));
    quasselTranslator->load(locale, QString(""), translationDirPath());
#else
    bool success = qtTranslator->load(QString("qt_%1").arg(locale.name()), translationDirPath());
    if (!success)
        qtTranslator->load(QString("qt_%1").arg(locale.name()), QLibraryInfo::location(QLibraryInfo::TranslationsPath));
    quasselTranslator->load(QString("%1").arg(locale.name()), translationDirPath());
#endif
}
