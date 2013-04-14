#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QtGui>
#include <QSqlDatabase>
#include <QSqlTableModel>
#include <QStringList>
#include <QSqlQuery>
#include <iterator>
#include <QPixmap>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSqlRecord>

#include "edittitle.h"
#include "preferences.h"

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT
    
public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

protected:
    void start();
    void checkDb();
    void addFilesToDb();
    void setMatchInfo(QJsonDocument);
    
private slots:
    void on_commandLinkButton_clicked();
    void httpFinished();
    void on_viewMatchButton_clicked();
    void on_editTitle_clicked();
    void on_actionPreferences_triggered();

private:
    QSettings *settings;
    QDir dir;
    Ui::MainWindow *ui;
    QSqlDatabase db;
    QSqlTableModel *model;
    QSqlQueryModel queryModel;
    QStringList list;
    QNetworkAccessManager *manager;
    QNetworkReply *reply;
    QString picDir;
};

#endif // MAINWINDOW_H