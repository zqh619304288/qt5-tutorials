/****************************************************************************
**
** Copyright (C) 2009 Nokia Corporation and/or its subsidiary(-ies).
** Contact: Qt Software Information (qt-info@nokia.com)
**
** This file is part of the Graphics Dojo project on Qt Labs.
**
** This file may be used under the terms of the GNU General Public
** License version 2.0 or 3.0 as published by the Free Software Foundation
** and appearing in the file LICENSE.GPL included in the packaging of
** this file.  Please review the following information to ensure GNU
** General Public Licensing requirements will be met:
** http://www.fsf.org/licensing/licenses/info/GPLv2.html and
** http://www.gnu.org/copyleft/gpl.html.
**
** If you are unsure which license is appropriate for your use, please
** contact the sales department at qt-sales@nokia.com.
**
** This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
** WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
**
****************************************************************************/

#include <QtCore>
#include <QtGui>
#include <QtNetwork>

#if defined (Q_OS_SYMBIAN)
#include "sym_iap_util.h"
#endif

#include "ui_form.h"

#define FLIGHTVIEW_URL "http://mobile.flightview.com/TrackByFlight.aspx"
#define FLIGHTVIEW_RANDOM "http://mobile.flightview.com/TrackSampleFlight.aspx"

// strips all invalid constructs that might trip QXmlStreamReader
static QString sanitized(const QString &xml)
{
    QString data = xml;

    // anything up to the html tag
    int i = data.indexOf("<html");
    if (i > 0)
        data.remove(0, i - 1);

    // everything inside the head tag
    i = data.indexOf("<head");
    if (i > 0)
        data.remove(i, data.indexOf("</head>") - i + 7);

    // invalid link for JavaScript code
    while (true) {
        i  = data.indexOf("onclick=\"gotoUrl(");
        if (i < 0)
            break;
        data.remove(i, data.indexOf('\"', i + 9) - i + 1);
    }

    // all inline frames
    while (true) {
        i  = data.indexOf("<iframe");
        if (i < 0)
            break;
        data.remove(i, data.indexOf("</iframe>") - i + 8);
    }

    // entities
    data.remove("&nbsp;");
    data.remove("&copy;");

    return data;
}

class FlightInfo : public QMainWindow
{
    Q_OBJECT

private:

    Ui_Form ui;
    QUrl m_url;
    QDate m_searchDate;
    QPixmap m_map;

public:

    FlightInfo(QMainWindow *parent = 0): QMainWindow(parent) {

        QWidget *w = new QWidget(this);
        ui.setupUi(w);
        setCentralWidget(w);

        ui.searchBar->hide();
        ui.infoBox->hide();
        connect(ui.searchButton, SIGNAL(clicked()), SLOT(startSearch()));
        connect(ui.flightEdit, SIGNAL(returnPressed()), SLOT(startSearch()));

        setWindowTitle("Flight Info");
        QTimer::singleShot(0, this, SLOT(delayedInit()));

        // Rendered from the public-domain vectorized aircraft
        // http://openclipart.org/media/people/Jarno
        m_map = QPixmap(":/aircraft.png");

        QAction *searchTodayAction = new QAction("Today's Flight", this);
        QAction *searchYesterdayAction = new QAction("Yesterday's Flight", this);
        QAction *randomAction = new QAction("Random Flight", this);
        connect(searchTodayAction, SIGNAL(triggered()), SLOT(today()));
        connect(searchYesterdayAction, SIGNAL(triggered()), SLOT(yesterday()));
        connect(randomAction, SIGNAL(triggered()), SLOT(randomFlight()));
#if defined(Q_OS_SYMBIAN)
        menuBar()->addAction(searchTodayAction);
        menuBar()->addAction(searchYesterdayAction);
        menuBar()->addAction(randomAction);
#else
        addAction(searchTodayAction);
        addAction(searchYesterdayAction);
        addAction(randomAction);
        setContextMenuPolicy(Qt::ActionsContextMenu);
#endif
    }

private slots:
    void delayedInit() {
#if defined(Q_OS_SYMBIAN)
        qt_SetDefaultIap();
#endif
    }


    void handleNetworkData(QNetworkReply *networkReply) {
        if (!networkReply->error()) {
            // Assume UTF-8 encoded
            QByteArray data = networkReply->readAll();
            QString xml = QString::fromUtf8(data);
            digest(xml);
        }
        networkReply->deleteLater();
        networkReply->manager()->deleteLater();
    }

    void handleMapData(QNetworkReply *networkReply) {
        if (!networkReply->error()) {
            m_map.loadFromData(networkReply->readAll());
            update();
        }
        networkReply->deleteLater();
        networkReply->manager()->deleteLater();
    }

    void today() {
        QDateTime timestamp = QDateTime::currentDateTime();
        m_searchDate = timestamp.date();
        searchFlight();
    }

    void yesterday() {
        QDateTime timestamp = QDateTime::currentDateTime();
        timestamp = timestamp.addDays(-1);
        m_searchDate = timestamp.date();
        searchFlight();
    }

    void searchFlight() {
        ui.searchBar->show();
        ui.infoBox->hide();
        ui.flightStatus->hide();
        ui.flightName->setText("Enter flight number");
        m_map = QPixmap();
        update();
    }

    void startSearch() {
        ui.searchBar->hide();
        QString flight = ui.flightEdit->text().simplified();
        if (!flight.isEmpty())
            request(flight, m_searchDate);
    }

    void randomFlight() {
        request(QString(), QDate::currentDate());
    }

public slots:

    void request(const QString &flightCode, const QDate &date) {

        setWindowTitle("Loading...");

        QString code = flightCode.simplified();
        QString airlineCode = code.left(2).toUpper();
        QString flightNumber = code.mid(2, code.length());

        ui.flightName->setText("Searching for " + code);

        m_url = QUrl(FLIGHTVIEW_URL);
        m_url.addEncodedQueryItem("view", "detail");
        m_url.addEncodedQueryItem("al", QUrl::toPercentEncoding(airlineCode));
        m_url.addEncodedQueryItem("fn", QUrl::toPercentEncoding(flightNumber));
        m_url.addEncodedQueryItem("dpdat", QUrl::toPercentEncoding(date.toString("yyyyMMdd")));

        if (code.isEmpty()) {
            // random flight as sample
            m_url = QUrl(FLIGHTVIEW_RANDOM);
            ui.flightName->setText("Getting a random flight...");
        }

        QNetworkAccessManager *manager = new QNetworkAccessManager(this);
        connect(manager, SIGNAL(finished(QNetworkReply*)),
                this, SLOT(handleNetworkData(QNetworkReply*)));
        manager->get(QNetworkRequest(m_url));
    }


private:

    void digest(const QString &content) {

        setWindowTitle("Flight Info");
        QString data = sanitized(content);

        // do we only get the flight list?
        // we grab the first leg in the flight list
        // then fetch another URL for the real flight info
        int i = data.indexOf("a href=\"?view=detail");
        if (i > 0) {
            QString href = data.mid(i, data.indexOf('\"', i + 8) - i + 1);
            QRegExp regex("dpap=([A-Za-z0-9]+)");
            regex.indexIn(href);
            QString airport = regex.cap(1);
            m_url.addEncodedQueryItem("dpap", QUrl::toPercentEncoding(airport));
            QNetworkAccessManager *manager = new QNetworkAccessManager(this);
            connect(manager, SIGNAL(finished(QNetworkReply*)),
                    this, SLOT(handleNetworkData(QNetworkReply*)));
            manager->get(QNetworkRequest(m_url));
            return;
        }

        QXmlStreamReader xml(data);
        bool inFlightName = false;
        bool inFlightStatus = false;
        bool inFlightMap = false;
        bool inFieldName = false;
        bool inFieldValue = false;

        QString flightName;
        QString flightStatus;
        QStringList fieldNames;
        QStringList fieldValues;

        while (!xml.atEnd()) {
            xml.readNext();

            if (xml.tokenType() == QXmlStreamReader::StartElement) {
                QStringRef className = xml.attributes().value("class");
                inFlightName |= xml.name() == "h1";
                inFlightStatus |= className == "FlightDetailHeaderStatus";
                inFlightMap |= className == "flightMap";
                if (xml.name() == "td" && !className.isEmpty()) {
                    QString cn = className.toString();
                    if (cn.contains("fieldTitle")) {
                        inFieldName = true;
                        fieldNames += QString();
                        fieldValues += QString();
                    }
                    if (cn.contains("fieldValue"))
                        inFieldValue = true;
                }
                if (xml.name() == "img" && inFlightMap) {
                    QString src = xml.attributes().value("src").toString();
                    src.prepend("http://mobile.flightview.com");
                    QUrl url = QUrl::fromPercentEncoding(src.toAscii());
                    QNetworkAccessManager *manager = new QNetworkAccessManager(this);
                    connect(manager, SIGNAL(finished(QNetworkReply*)),
                            this, SLOT(handleMapData(QNetworkReply*)));
                    manager->get(QNetworkRequest(url));
                }
            }

            if (xml.tokenType() == QXmlStreamReader::EndElement) {
                inFlightName &= xml.name() != "h1";
                inFlightStatus &= xml.name() != "div";
                inFlightMap &= xml.name() != "div";
                inFieldName &= xml.name() != "td";
                inFieldValue &= xml.name() != "td";
            }

            if (xml.tokenType() == QXmlStreamReader::Characters) {
                if (inFlightName)
                    flightName += xml.text();
                if (inFlightStatus)
                    flightStatus += xml.text();
                if (inFieldName)
                    fieldNames.last() += xml.text();
                if (inFieldValue)
                    fieldValues.last() += xml.text();
            }
        }

        if (fieldNames.isEmpty()) {
            QString code = ui.flightEdit->text().simplified().left(10);
            QString msg = QString("Flight %1 is not found").arg(code);
            ui.flightName->setText(msg);
            return;
        }

        ui.flightName->setText(flightName);
        flightStatus.remove("Status: ");
        ui.flightStatus->setText(flightStatus);
        ui.flightStatus->show();

        QStringList whiteList;
        whiteList << "Departure";
        whiteList << "Arrival";
        whiteList << "Scheduled";
        whiteList << "Takeoff";
        whiteList << "Estimated";
        whiteList << "Term-Gate";

        QString text;
        text = QString("<table width=%1>").arg(width() - 25);
        for (int i = 0; i < fieldNames.count(); i++) {
            QString fn = fieldNames[i].simplified();
            if (fn.endsWith(':'))
                fn = fn.left(fn.length() - 1);
            if (!whiteList.contains(fn))
                continue;

            QString fv = fieldValues[i].simplified();
            bool special = false;
            special |= fn.startsWith("Departure");
            special |= fn.startsWith("Arrival");
            text += "<tr>";
            if (special) {
                text += "<td align=center colspan=2>";
                text += "<b><font size=+1>" + fv + "</font></b>";
                text += "</td>";
            } else {
                text += "<td align=right>";
                text += fn;
                text += " : ";
                text += "&nbsp;";
                text += "</td>";
                text += "<td>";
                text += fv;
                text += "</td>";
            }
            text += "</tr>";
        }
        text += "</table>";
        ui.detailedInfo->setText(text);
        ui.infoBox->show();
    }

    void resizeEvent(QResizeEvent *event) {
        Q_UNUSED(event);
        ui.detailedInfo->setMaximumWidth(width() - 25);
    }

    void paintEvent(QPaintEvent *event) {
        QMainWindow::paintEvent(event);
        QPainter p(this);
        p.fillRect(rect(), QColor(131, 171, 210));
        if (!m_map.isNull()) {
            int x = (width() - m_map.width()) / 2;
            int space = ui.infoBox->pos().y();
            if (!ui.infoBox->isVisible())
                space = height();
            int top = ui.titleBox->height();
            int y = qMax(top, (space - m_map.height()) / 2);
            p.drawPixmap(x, y, m_map);
        }
        p.end();
    }

};


#include "flightinfo.moc"

int main(int argc, char **argv)
{
    Q_INIT_RESOURCE(flightinfo);

    QApplication app(argc, argv);

    FlightInfo w;
#if defined(Q_OS_SYMBIAN)
    w.showMaximized();
#else
    w.resize(360, 504);
    w.show();
#endif

    return app.exec();
}
