#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    //create blank image for empty item slots
    image = QPixmap(QSize(32,24));
    image.fill(Qt::black);

    initializeUIPointers();
    start();
    addFilesToDb();
    connect(ui->tableView, SIGNAL(doubleClicked(QModelIndex)), this, SLOT(on_tableView_clicked(QModelIndex)));
}


MainWindow::~MainWindow()
{
    settings->setValue("windowGeometry", saveGeometry());
    settings->setValue("windowState", saveState());
    settings->sync();
    db.exec(QString("VACUUM"));
    db.close();

    delete settings;
    delete model;
    delete ui;
}

void MainWindow::start()
{
    userDir = QStandardPaths::standardLocations(QStandardPaths::DataLocation).at(0);

    //create the folder if it doesn't already exist
    if(!QDir(userDir).exists())
    {
        userDir.mkpath(userDir.absolutePath());
    }

    settings = new QSettings(userDir.absolutePath() + "/settings.ini", QSettings::IniFormat);
    apiKey = settings->value("apiKey").toString();

    dir = settings->value("replayFolder", "C:/Program Files (x86)/Steam/SteamApps/common/dota 2 beta/dota/replays").toString();
    restoreGeometry(settings->value("windowGeometry", "").toByteArray()); //restore previous session's dimensions of the program
    restoreState(settings->value("windowState", "").toByteArray()); //restore the previous session's state of the program

    //set buttons to disabled until user selects a valid row
    ui->watchReplay->setEnabled(false);
    ui->editTitle->setEnabled(false);
    ui->viewMatchButton->setEnabled(false);
    ui->deleteReplayButton->setEnabled(false);

    //font settings
    font.setPointSize(settings->value("fontSize", "10").toInt());
    font.setFamily(settings->value("fontFamily", "Times New Roman").toString());

    db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(userDir.absolutePath() + "/matches.db");
    db.open();
    db.exec( "create table if not exists replays (title TEXT, filename TEXT PRIMARY KEY, fileExists BLOB)" );
    model = new QSqlTableModel(this, db);
    ui->tableView->setModel(model);
    model->setTable("replays");
    model->select();
    ui->tableView->hideColumn(2);
}

void MainWindow::checkDb() //check and remove files from db that are no longer locally saved
{
    db.transaction();
    QSqlQuery query;
    QSqlQuery updateRow;
    query.exec("update replays set fileExists = 0");
    updateRow.prepare("update replays set fileExists = 1 where filename = :filename");
    query.prepare("select * from replays where filename = :id");
    int i=0;
    for(QStringList::Iterator it = list.begin(); it != list.end(); it++)
    {
        QString param = QString(list.at(i));
        qDebug() << param;
        query.bindValue(":id", param);
        if(query.exec())
        {
            while(query.next())
            {
                updateRow.bindValue(0, param);
                updateRow.exec();
            }
        }
        else
        {
            qDebug() << "query did not exec;";
        }
        i++;
    }
    query.exec("delete from replays where fileExists = 0");
    db.commit();
}

void MainWindow::addFilesToDb()
{
    //Disable buttons since nothing will be selected
    ui->watchReplay->setEnabled(false);
    ui->editTitle->setEnabled(false);
    ui->viewMatchButton->setEnabled(false);
    ui->deleteReplayButton->setEnabled(false);

    list.clear();
    QSqlQuery query;
    db.transaction();
    query.prepare("insert into replays (fileExists, filename) VALUES (1, :filename)");
    bool ok = dir.exists();  //check if directory exist
        if ( ok )
        {
            //set fileinfo filter
            QFileInfoList entries = dir.entryInfoList( QDir::NoDotAndDotDot |
                    QDir::Dirs | QDir::Files );
                    //loop over entries filter selected
            foreach ( QFileInfo entryInfo, entries )
            {
                //QString path = entryInfo.absoluteFilePath();
                QString fileName = entryInfo.fileName();

                if ( entryInfo.isDir() )    //check if entryInfo is dir
                {
                }
                else
                {
                    if(fileName.endsWith(".dem"))
                    {
                        list.append(fileName);
                        query.bindValue(":filename", fileName);
                        query.exec();
                    }
                }
            }
        }

        if (ok && !dir.exists(dir.absolutePath()))
            ok = false;

        db.commit();
        checkDb();
        model->select();
        ui->tableView->resizeColumnsToContents();
}

/*
 * Valid types are 'heroes' or 'items'
 * name is the name of the hero/item your are fetching
 */
QPixmap MainWindow::getImage(QString type, QString name)
{
    //return empty QPixmap because the item slot was empty, so no need to try and fetch something that will fail and waste time for a failed request.
    if(name.compare("empty") == 0)
        return image;

    QPixmap pic;
    QString size;
    int width;

    if(type.compare("heroes") == 0)
    {
        size = "sb";
        width = 45;
    }
    else
    {
        size = "lg";
        width = 32;
    }
    pic.load("downloads/" + name + "_" + size + ".png");

    return pic.scaledToWidth(width);
}

void MainWindow::on_watchReplay_clicked()
{
    queryModel.setQuery("SELECT * FROM replays");
    QDialog dialog(this);
    QVBoxLayout *layout = new QVBoxLayout;
    QTextEdit *textEdit = new QTextEdit;
    textEdit->setReadOnly(true);
    layout->addWidget(textEdit);
    textEdit->setHtml(QString("Type This Into Dota 2 Console: <p> <pre>playdemo replays/%1</pre><p><em>make sure the replay is in your default dota 2 replay directory</em>").arg(queryModel.record(ui->tableView->selectionModel()->currentIndex().row()).value("filename").toString()));
    QDialogButtonBox *buttonBox = new QDialogButtonBox;
    QPushButton *acceptButton = new QPushButton(tr("Ok"));
    buttonBox->addButton(acceptButton, QDialogButtonBox::AcceptRole);
    layout->addWidget(buttonBox);

    dialog.setLayout(layout);
    dialog.connect(buttonBox, SIGNAL(accepted()), SLOT(close()));
    dialog.exec();
    acceptButton->deleteLater();
    buttonBox->deleteLater();
    textEdit->deleteLater();
    layout->deleteLater();
}

void MainWindow::setMatchInfo()
{
    //we need to make sure we don't use this slot anymore since this is in the slot.
    //if we kept this slot enabled we would keep calling this everytime a download is finished and continous loop.
    http.disconnect();

    ui->tabWidget->setCurrentIndex(0);

    QString matchID = queryModel.record(ui->tableView->selectionModel()->currentIndex().row()).value("filename").toString().remove(".dem");
    QFile file("downloads/" + matchID + ".json");

    //test matchInfo parse class
    matchInfo MatchParser;
    MatchParser.parse("downloads/" + matchID + ".json");

    //display basic match info
    ui->matchID->setText( MatchParser.getMatchID() );
    ui->gameMode->setText( MatchParser.getGameMode() );
    ui->startTime->setText( MatchParser.getStartTime() );
    ui->lobbyType->setText( MatchParser.getLobbyType() );
    ui->duration->setText( MatchParser.getDuration() );
    ui->fbTime->setText( MatchParser.getFirstBloodTime() );

    playerheroPicUI[0][0]->setText("<img src='downloads/necrolyte_sb.png' width='45' />");

    //if CM, then display picks & bans
    if(MatchParser.getGameMode().compare("Captains Mode") == 0)
    {
        QString img;
        for(int i=0; i < 5; i++)
        {
            radiantBansUI[i]->setText("<img src=\"downloads/" + MatchParser.getBans()[0][i] + "_sb.png\" width=\"45\" />");
            radiantPicksUI[i]->setText("<img src=\"downloads/" + MatchParser.getPicks()[0][i] + "_sb.png\" width=\"45\" />");
        }

        for(int i=0; i<5; i++)
        {
            //Pixmap display higher quality, but using html <img> is easier but does not display as good of quality
            //QPixmap pic;
            //pic.load("downloads/" + MatchParser.getBans()[1][i] + "_sb.png");

            direBansUI[i]->setText("<img src=\"downloads/" + MatchParser.getBans()[1][i] + "_sb.png\" width=\"45\" />");
            direPicksUI[i]->setText("<img src=\"downloads/" + MatchParser.getPicks()[1][i] + "_sb.png\" width=\"45\" />");
        }
    }

    /*
     * file.open(QIODevice::ReadOnly);
    QJsonDocument json = QJsonDocument::fromJson(file.readAll());

    //set winner
    if(json.object().value("radiant_win").toString().compare("1") == 0)
        ui->winner->setText("<font color=\"green\">Radiant Victory</font>");
    else
        ui->winner->setText("<font color=\"red\">Dire Victory</font>");

    //main match info
    ui->matchID->setText(QString("<a href=\"http://dotabuff.com/matches/%1\">%1</a>").arg(json.object().value("match_id").toString()));

    ui->gameMode->setText(json.object().value("game_mode").toString());
    ui->startTime->setText(json.object().value("start_time").toString());
    ui->lobbyType->setText(json.object().value("lobby_type").toString());
    ui->duration->setText(json.object().value("duration").toString());
    ui->fbTime->setText(json.object().value("first_blood_time").toString());

    //picks and bans
    if(json.object().value("game_mode").toString().compare("Captains Mode") == 0)
    {
        QJsonArray radiantBans = json.object().value("picks_bans").toObject().value("radiant").toObject().value("bans").toArray();
        for(int i=0; i < 5; i++)
        {
            http.append(QUrl(baseUrl + radiantBans.at(i).toObject().value("name").toString() + "_sb.png"));
        }

        for(int i=0; i < 5; i++)
            radiantBansUI[i]->setPixmap(getImage(QString("heroes"), radiantBans.at(i).toObject().value("name").toString()));

        QJsonArray radiantPicks = json.object().value("picks_bans").toObject().value("radiant").toObject().value("picks").toArray();
        //ui->radiantPick_1->setPixmap(QPixmap(picDir + "heroes/JPEG/" + radiantPicks.at(0).toObject().value("name").toString() + ".jpg"));
        ui->radiantPick_1->setPixmap(getImage("heroes", radiantPicks.at(0).toObject().value("name").toString() ));
        ui->radiantPick_2->setPixmap(getImage("heroes", radiantPicks.at(1).toObject().value("name").toString() ));
        ui->radiantPick_3->setPixmap(getImage("heroes", radiantPicks.at(2).toObject().value("name").toString() ));
        ui->radiantPick_4->setPixmap(getImage("heroes", radiantPicks.at(3).toObject().value("name").toString() ));
        ui->radiantPick_5->setPixmap(getImage("heroes", radiantPicks.at(4).toObject().value("name").toString() ));

        QJsonArray direBans = json.object().value("picks_bans").toObject().value("dire").toObject().value("bans").toArray();
        //ui->direBan_1->setPixmap(QPixmap(picDir + "heroes/JPEG/" + direBans.at(0).toObject().value("name").toString() + ".jpg"));
        ui->direBan_1->setPixmap(getImage( "heroes", direBans.at(0).toObject().value("name").toString() ));
        ui->direBan_2->setPixmap(getImage( "heroes", direBans.at(1).toObject().value("name").toString() ));
        ui->direBan_3->setPixmap(getImage( "heroes", direBans.at(2).toObject().value("name").toString() ));
        ui->direBan_4->setPixmap(getImage( "heroes", direBans.at(3).toObject().value("name").toString() ));
        ui->direBan_5->setPixmap(getImage( "heroes", direBans.at(4).toObject().value("name").toString() ));

        QJsonArray direPicks = json.object().value("picks_bans").toObject().value("dire").toObject().value("picks").toArray();
        //ui->direPick_1->setPixmap(QPixmap(picDir + "heroes/JPEG/" + direPicks.at(0).toObject().value("name").toString() + ".jpg"));
        ui->direPick_1->setPixmap(getImage( "heroes", direPicks.at(0).toObject().value("name").toString() ));
        ui->direPick_2->setPixmap(getImage( "heroes", direPicks.at(1).toObject().value("name").toString() ));
        ui->direPick_3->setPixmap(getImage( "heroes", direPicks.at(2).toObject().value("name").toString() ));
        ui->direPick_4->setPixmap(getImage( "heroes", direPicks.at(3).toObject().value("name").toString() ));
        ui->direPick_5->setPixmap(getImage( "heroes", direPicks.at(4).toObject().value("name").toString() ));
    }
    else
    {
        //clear picks and bans because the selected match was not in CM
        for(int i=0; i < 5; i++)
        {
            radiantBansUI[i]->clear();
            radiantPicksUI[i]->clear();
            direBansUI[i]->clear();
            direPicksUI[i]->clear();
        }
    }

    //radiant
    // radiant Player Names
    QJsonArray radiantSlots = json.object().value("slots").toObject().value("radiant").toArray();
    for(int i=0; i<5; i++)
        playerNameUI[0][i]->setText(radiantSlots.at(i).toObject().value("account_name").toString());

    //radiant levels
    for(int i=0; i<5; i++)
        playerLevelUI[0][i]->setText(radiantSlots.at(i).toObject().value("level").toString());

    //radiant Hero Pix
    for(int i=0; i < 5; i++)
        http.append(QUrl(baseUrl + "heroes/" + radiantSlots.at(i).toObject().value("hero").toObject().value("name").toString() + "_sb.png"));

    for(int i=0; i<5;i++)
        radiantHeroPicUI[i]->setPixmap(getImage( "heroes", radiantSlots.at(i).toObject().value("hero").toObject().value("name").toString() ));

    // radiant Hero Names
    for(int i=0 ; i<5; i++)
        playerHeroNameUI[0][i]->setText(radiantSlots.at(i).toObject().value("hero").toObject().value("localized_name").toString());

    //radiant Kills
    for(int i=0; i<5; i++)
        playerKillsUI[0][i]->setText(radiantSlots.at(i).toObject().value("kills").toString());

    //radiant Deaths
    for(int i=0; i<5; i++)
        playerDeathsUI[0][i]->setText(radiantSlots.at(i).toObject().value("deaths").toString());

    //radiant Assists
    for(int i=0; i<5; i++)
        playerAssistsUI[0][i]->setText(radiantSlots.at(i).toObject().value("assists").toString());

    //radiant Items
    for(int i=0; i<5; i++)
        for(int j=0; j<6; j++)
        {
            //trying to get an image named empty is a waste of time since it does not exist, so just skip it.
            if(radiantSlots.at(i).toObject().value( QString("item_") + QString::number(j) ).toString().compare("empty") != 0)
            {
                http.append(baseUrl + "items/" + radiantSlots.at(i).toObject().value( QString("item_") + QString::number(j) ).toString() + "_lg.png");
                playerItemsUI[0][i][j]->setPixmap(getImage( "items", radiantSlots.at(i).toObject().value( QString("item_") + QString::number(j) ).toString() ));
            }
        }

    //wait for downloads to complete, so we can have images to display
    while(!http.isFinished())
        QApplication::processEvents();

    //radiant gold spent
    ui->radiantGold_1->setText(radiantSlots.at(0).toObject().value("gold_spent").toString());
    ui->radiantGold_2->setText(radiantSlots.at(1).toObject().value("gold_spent").toString());
    ui->radiantGold_3->setText(radiantSlots.at(2).toObject().value("gold_spent").toString());
    ui->radiantGold_4->setText(radiantSlots.at(3).toObject().value("gold_spent").toString());
    ui->radiantGold_5->setText(radiantSlots.at(4).toObject().value("gold_spent").toString());

    //radiant Last Hits
    ui->radiantLH_1->setText(radiantSlots.at(0).toObject().value("last_hits").toString());
    ui->radiantLH_2->setText(radiantSlots.at(1).toObject().value("last_hits").toString());
    ui->radiantLH_3->setText(radiantSlots.at(2).toObject().value("last_hits").toString());
    ui->radiantLH_4->setText(radiantSlots.at(3).toObject().value("last_hits").toString());
    ui->radiantLH_5->setText(radiantSlots.at(4).toObject().value("last_hits").toString());

    //radiant Denies
    ui->radiantDN_1->setText(radiantSlots.at(0).toObject().value("denies").toString());
    ui->radiantDN_2->setText(radiantSlots.at(1).toObject().value("denies").toString());
    ui->radiantDN_3->setText(radiantSlots.at(2).toObject().value("denies").toString());
    ui->radiantDN_4->setText(radiantSlots.at(3).toObject().value("denies").toString());
    ui->radiantDN_5->setText(radiantSlots.at(4).toObject().value("denies").toString());

    //radiant Gold/Min
    ui->radiantGPM_1->setText(radiantSlots.at(0).toObject().value("gold_per_min").toString());
    ui->radiantGPM_2->setText(radiantSlots.at(1).toObject().value("gold_per_min").toString());
    ui->radiantGPM_3->setText(radiantSlots.at(2).toObject().value("gold_per_min").toString());
    ui->radiantGPM_4->setText(radiantSlots.at(3).toObject().value("gold_per_min").toString());
    ui->radiantGPM_5->setText(radiantSlots.at(4).toObject().value("gold_per_min").toString());

    //radiant XP/Min
    ui->radiantXPM_1->setText(radiantSlots.at(0).toObject().value("xp_per_min").toString());
    ui->radiantXPM_2->setText(radiantSlots.at(1).toObject().value("xp_per_min").toString());
    ui->radiantXPM_3->setText(radiantSlots.at(2).toObject().value("xp_per_min").toString());
    ui->radiantXPM_4->setText(radiantSlots.at(3).toObject().value("xp_per_min").toString());
    ui->radiantXPM_5->setText(radiantSlots.at(4).toObject().value("xp_per_min").toString());
    //end radiant

    //dire
    QJsonArray direSlots = json.object().value("slots").toObject().value("dire").toArray();
    //dire player names
    ui->direPlayer_1->setText(direSlots.at(0).toObject().value("account_name").toString());
    ui->direPlayer_2->setText(direSlots.at(1).toObject().value("account_name").toString());
    ui->direPlayer_3->setText(direSlots.at(2).toObject().value("account_name").toString());
    ui->direPlayer_4->setText(direSlots.at(3).toObject().value("account_name").toString());
    ui->direPlayer_5->setText(direSlots.at(4).toObject().value("account_name").toString());

    //dire Levels
    ui->direLevel_1->setText(direSlots.at(0).toObject().value("level").toString());
    ui->direLevel_2->setText(direSlots.at(1).toObject().value("level").toString());
    ui->direLevel_3->setText(direSlots.at(2).toObject().value("level").toString());
    ui->direLevel_4->setText(direSlots.at(3).toObject().value("level").toString());
    ui->direLevel_5->setText(direSlots.at(4).toObject().value("level").toString());

    //dire Hero Pix
    for(int i=0; i<5; i++)
        http.append(QUrl("http://media.steampowered.com/apps/dota2/images/heroes/" + direSlots.at(i).toObject().value("hero").toObject().value("name").toString() + "_sb.png"));
    ui->direHeroPic_1->setPixmap(getImage( "heroes", direSlots.at(0).toObject().value("hero").toObject().value("name").toString() ));
    ui->direHeroPic_2->setPixmap(getImage( "heroes", direSlots.at(1).toObject().value("hero").toObject().value("name").toString() ));
    ui->direHeroPic_3->setPixmap(getImage( "heroes", direSlots.at(2).toObject().value("hero").toObject().value("name").toString() ));
    ui->direHeroPic_4->setPixmap(getImage( "heroes", direSlots.at(3).toObject().value("hero").toObject().value("name").toString() ));
    ui->direHeroPic_5->setPixmap(getImage( "heroes", direSlots.at(4).toObject().value("hero").toObject().value("name").toString() ));

    //dire Hero Names
    ui->direHero_1->setText(direSlots.at(0).toObject().value("hero").toObject().value("localized_name").toString());
    ui->direHero_2->setText(direSlots.at(1).toObject().value("hero").toObject().value("localized_name").toString());
    ui->direHero_3->setText(direSlots.at(2).toObject().value("hero").toObject().value("localized_name").toString());
    ui->direHero_4->setText(direSlots.at(3).toObject().value("hero").toObject().value("localized_name").toString());
    ui->direHero_5->setText(direSlots.at(4).toObject().value("hero").toObject().value("localized_name").toString());

    //dire Kills
    ui->direKills_1->setText(direSlots.at(0).toObject().value("kills").toString());
    ui->direKills_2->setText(direSlots.at(1).toObject().value("kills").toString());
    ui->direKills_3->setText(direSlots.at(2).toObject().value("kills").toString());
    ui->direKills_4->setText(direSlots.at(3).toObject().value("kills").toString());
    ui->direKills_5->setText(direSlots.at(4).toObject().value("kills").toString());

    //dire Deaths
    ui->direDeaths_1->setText(direSlots.at(0).toObject().value("deaths").toString());
    ui->direDeaths_2->setText(direSlots.at(1).toObject().value("deaths").toString());
    ui->direDeaths_3->setText(direSlots.at(2).toObject().value("deaths").toString());
    ui->direDeaths_4->setText(direSlots.at(3).toObject().value("deaths").toString());
    ui->direDeaths_5->setText(direSlots.at(4).toObject().value("deaths").toString());

    //dire Assists
    ui->direAssists_1->setText(direSlots.at(0).toObject().value("assists").toString());
    ui->direAssists_2->setText(direSlots.at(1).toObject().value("assists").toString());
    ui->direAssists_3->setText(direSlots.at(2).toObject().value("assists").toString());
    ui->direAssists_4->setText(direSlots.at(3).toObject().value("assists").toString());
    ui->direAssists_5->setText(direSlots.at(4).toObject().value("assists").toString());

    //dire Items
    //player 1
    ui->direItems_1_1->setPixmap(getImage( "items", direSlots.at(0).toObject().value("item_0").toString() ));
    ui->direItems_1_2->setPixmap(getImage( "items", direSlots.at(0).toObject().value("item_1").toString() ));
    ui->direItems_1_3->setPixmap(getImage( "items", direSlots.at(0).toObject().value("item_2").toString() ));
    ui->direItems_1_4->setPixmap(getImage( "items", direSlots.at(0).toObject().value("item_3").toString() ));
    ui->direItems_1_5->setPixmap(getImage( "items", direSlots.at(0).toObject().value("item_4").toString() ));
    ui->direItems_1_6->setPixmap(getImage( "items", direSlots.at(0).toObject().value("item_5").toString() ));

    //player 2
    ui->direItems_2_1->setPixmap(getImage( "items", direSlots.at(1).toObject().value("item_0").toString() ));
    ui->direItems_2_2->setPixmap(getImage( "items", direSlots.at(1).toObject().value("item_1").toString() ));
    ui->direItems_2_3->setPixmap(getImage( "items", direSlots.at(1).toObject().value("item_2").toString() ));
    ui->direItems_2_4->setPixmap(getImage( "items", direSlots.at(1).toObject().value("item_3").toString() ));
    ui->direItems_2_5->setPixmap(getImage( "items", direSlots.at(1).toObject().value("item_4").toString() ));
    ui->direItems_2_6->setPixmap(getImage( "items", direSlots.at(1).toObject().value("item_5").toString() ));

    //player 3
    ui->direItems_3_1->setPixmap(getImage( "items", direSlots.at(2).toObject().value("item_0").toString() ));
    ui->direItems_3_2->setPixmap(getImage( "items", direSlots.at(2).toObject().value("item_1").toString() ));
    ui->direItems_3_3->setPixmap(getImage( "items", direSlots.at(2).toObject().value("item_2").toString() ));
    ui->direItems_3_4->setPixmap(getImage( "items", direSlots.at(2).toObject().value("item_3").toString() ));
    ui->direItems_3_5->setPixmap(getImage( "items", direSlots.at(2).toObject().value("item_4").toString() ));
    ui->direItems_3_6->setPixmap(getImage( "items", direSlots.at(2).toObject().value("item_5").toString() ));

    //player 4
    ui->direItems_4_1->setPixmap(getImage( "items", direSlots.at(3).toObject().value("item_0").toString() ));
    ui->direItems_4_2->setPixmap(getImage( "items", direSlots.at(3).toObject().value("item_1").toString() ));
    ui->direItems_4_3->setPixmap(getImage( "items", direSlots.at(3).toObject().value("item_2").toString() ));
    ui->direItems_4_4->setPixmap(getImage( "items", direSlots.at(3).toObject().value("item_3").toString() ));
    ui->direItems_4_5->setPixmap(getImage( "items", direSlots.at(3).toObject().value("item_4").toString() ));
    ui->direItems_4_6->setPixmap(getImage( "items", direSlots.at(3).toObject().value("item_5").toString() ));

    //player 5
    ui->direItems_5_1->setPixmap(getImage( "items", direSlots.at(4).toObject().value("item_0").toString() ));
    ui->direItems_5_2->setPixmap(getImage( "items", direSlots.at(4).toObject().value("item_1").toString() ));
    ui->direItems_5_3->setPixmap(getImage( "items", direSlots.at(4).toObject().value("item_2").toString() ));
    ui->direItems_5_4->setPixmap(getImage( "items", direSlots.at(4).toObject().value("item_3").toString() ));
    ui->direItems_5_5->setPixmap(getImage( "items", direSlots.at(4).toObject().value("item_4").toString() ));
    ui->direItems_5_6->setPixmap(getImage( "items", direSlots.at(4).toObject().value("item_5").toString() ));

    //dire Gold
    ui->direGold_1->setText(direSlots.at(0).toObject().value("gold_spent").toString());
    ui->direGold_2->setText(direSlots.at(1).toObject().value("gold_spent").toString());
    ui->direGold_3->setText(direSlots.at(2).toObject().value("gold_spent").toString());
    ui->direGold_4->setText(direSlots.at(3).toObject().value("gold_spent").toString());
    ui->direGold_5->setText(direSlots.at(4).toObject().value("gold_spent").toString());

    //dire Last Hits
    ui->direLH_1->setText(direSlots.at(0).toObject().value("last_hits").toString());
    ui->direLH_2->setText(direSlots.at(1).toObject().value("last_hits").toString());
    ui->direLH_3->setText(direSlots.at(2).toObject().value("last_hits").toString());
    ui->direLH_4->setText(direSlots.at(3).toObject().value("last_hits").toString());
    ui->direLH_5->setText(direSlots.at(4).toObject().value("last_hits").toString());

    //dire Denies
    ui->direDN_1->setText(direSlots.at(0).toObject().value("denies").toString());
    ui->direDN_2->setText(direSlots.at(1).toObject().value("denies").toString());
    ui->direDN_3->setText(direSlots.at(2).toObject().value("denies").toString());
    ui->direDN_4->setText(direSlots.at(3).toObject().value("denies").toString());
    ui->direDN_5->setText(direSlots.at(4).toObject().value("denies").toString());

    //dire GPM
    ui->direGPM_1->setText(direSlots.at(0).toObject().value("gold_per_min").toString());
    ui->direGPM_2->setText(direSlots.at(1).toObject().value("gold_per_min").toString());
    ui->direGPM_3->setText(direSlots.at(2).toObject().value("gold_per_min").toString());
    ui->direGPM_4->setText(direSlots.at(3).toObject().value("gold_per_min").toString());
    ui->direGPM_5->setText(direSlots.at(4).toObject().value("gold_per_min").toString());

    //dire XPM
    ui->direXPM_1->setText(direSlots.at(0).toObject().value("xp_per_min").toString());
    ui->direXPM_2->setText(direSlots.at(1).toObject().value("xp_per_min").toString());
    ui->direXPM_3->setText(direSlots.at(2).toObject().value("xp_per_min").toString());
    ui->direXPM_4->setText(direSlots.at(3).toObject().value("xp_per_min").toString());
    ui->direXPM_5->setText(direSlots.at(4).toObject().value("xp_per_min").toString());

    //end dire
    */
    ui->statusBar->showMessage("Loading Complete!", 30000);     //display message in status bar for 30 sec.
}

void MainWindow::on_viewMatchButton_clicked()
{
    //check if apiKey is not set
    if(apiKey.isEmpty())
    {
        QMessageBox::information(this, "Api Key", "Make sure you set your api key in the preferences");
        return;
    }
    ui->statusBar->showMessage("Loading...");

    queryModel.setQuery("SELECT * FROM replays");
    QString matchID = queryModel.record(ui->tableView->selectionModel()->currentIndex().row()).value("filename").toString().remove(".dem");
    downloadMatch(matchID);
}

void MainWindow::on_editTitle_clicked()
{
    queryModel.setQuery("SELECT * FROM replays");
    EditTitle title;
    title.setTitle(queryModel.record(ui->tableView->selectionModel()->currentIndex().row()).value("title").toString());
    if(title.exec())
    {
        QSqlQuery query("update replays set title = :title WHERE filename = :filename");
        query.bindValue(0, title.getTitle());
        query.bindValue(1, queryModel.record(ui->tableView->selectionModel()->currentIndex().row()).value("filename").toString());
        query.exec();
        model->select();
        ui->tableView->resizeColumnsToContents();
    }
}

void MainWindow::on_actionPreferences_triggered()
{
    Preferences pref;
    pref.setDir(settings->value("replayFolder").toString());
    pref.setApiKey(apiKey);
    if(pref.exec())
    {
        dir = pref.getDir();
        apiKey = pref.getApiKey();
        settings->setValue("replayFolder", pref.getDir());
        settings->setValue("apiKey", apiKey);
        settings->sync();
        addFilesToDb();
    }
}

void MainWindow::on_actionClear_Cache_triggered()
{
}

void MainWindow::on_deleteReplayButton_clicked()
{
    queryModel.setQuery("SELECT * FROM replays");
    QString filename = queryModel.record(ui->tableView->selectionModel()->currentIndex().row()).value("filename").toString();
    QFile::remove(dir.absolutePath() + "/" + filename);
    addFilesToDb();
    ui->deleteReplayButton->setEnabled(false);
}

void MainWindow::downloadMatch(QString id)
{
    http.append("https://computerfr33k-dota-2-replay-manager.p.mashape.com/json-mashape.php?match_id=" + id);
    http.setRawHeader(QByteArray("X-Mashape-Authorization"), apiKey.toLatin1());
    connect(&http, SIGNAL(finished()), SLOT(setMatchInfo()));
}

void MainWindow::on_actionAbout_Qt_triggered()
{
    QMessageBox::aboutQt(this, tr("About Qt"));
}

void MainWindow::on_actionAbout_triggered()
{
    QMessageBox::about(this, tr("About"), tr("Version: %1 \nCreated by: Computerfr33k").arg(QApplication::applicationVersion()));
}

void MainWindow::on_actionWebsite_triggered()
{
    QDesktopServices::openUrl(QUrl("http://www.dota2replay-manager.com"));
}

void MainWindow::on_tableView_clicked(const QModelIndex &index)
{
    if(index.row() < 0)
    {
        ui->watchReplay->setEnabled(false);
        ui->editTitle->setEnabled(false);
        ui->viewMatchButton->setEnabled(false);
        ui->deleteReplayButton->setEnabled(false);
    }
    else
    {
        ui->watchReplay->setEnabled(true);
        ui->editTitle->setEnabled(true);
        ui->viewMatchButton->setEnabled(true);
        ui->deleteReplayButton->setEnabled(true);
    }
}

void MainWindow::on_refreshButton_clicked()
{
    addFilesToDb();
}

void MainWindow::on_actionCheck_For_Updates_triggered()
{
#ifdef Q_OS_WIN32
    //open the updater with admin priv, so if there is an update we can download and install it.
    QDesktopServices::openUrl(QUrl("file:///" + QDir::currentPath() + "/autoupdate/autoupdate-windows.exe", QUrl::TolerantMode));
#endif
#ifdef Q_OS_LINUX
    QProcess::startDetached("autoupdate/autoupdate-linux.run");
#endif
}

void MainWindow::networkError()
{
}

void MainWindow::sslError()
{
}

void MainWindow::initializeUIPointers()
{
    //create our QLabel array so we can for loop our match info
    // 2-D Array of QLabels; first index: 0 = radiant, 1 = Dire
    radiantBansUI[0] = ui->radiantBan_1;
    radiantBansUI[1] = ui->radiantBan_2;
    radiantBansUI[2] = ui->radiantBan_3;
    radiantBansUI[3] = ui->radiantBan_4;
    radiantBansUI[4] = ui->radiantBan_5;

    radiantPicksUI[0] = ui->radiantPick_1;
    radiantPicksUI[1] = ui->radiantPick_2;
    radiantPicksUI[2] = ui->radiantPick_3;
    radiantPicksUI[3] = ui->radiantPick_4;
    radiantPicksUI[4] = ui->radiantPick_5;

    radiantHeroPicUI[0] = ui->radiantHeroPic_1;
    radiantHeroPicUI[1] = ui->radiantHeroPic_2;
    radiantHeroPicUI[2] = ui->radiantHeroPic_3;
    radiantHeroPicUI[3] = ui->radiantHeroPic_4;
    radiantHeroPicUI[4] = ui->radiantHeroPic_5;

    direBansUI[0] = ui->direBan_1;
    direBansUI[1] = ui->direBan_2;
    direBansUI[2] = ui->direBan_3;
    direBansUI[3] = ui->direBan_4;
    direBansUI[4] = ui->direBan_5;

    direPicksUI[0] = ui->direPick_1;
    direPicksUI[1] = ui->direPick_2;
    direPicksUI[2] = ui->direPick_3;
    direPicksUI[3] = ui->direPick_4;
    direPicksUI[4] = ui->direPick_5;

    //radiant Player Names
    playerNameUI[0][0] = ui->radiantPlayer_1;
    playerNameUI[0][1] = ui->radiantPlayer_2;
    playerNameUI[0][2] = ui->radiantPlayer_3;
    playerNameUI[0][3] = ui->radiantPlayer_4;
    playerNameUI[0][4] = ui->radiantPlayer_5;
    //Dire Player Names
    playerNameUI[1][0] = ui->direPlayer_1;
    playerNameUI[1][1] = ui->direPlayer_2;
    playerNameUI[1][2] = ui->direPlayer_3;
    playerNameUI[1][3] = ui->direPlayer_4;
    playerNameUI[1][4] = ui->direPlayer_5;

    //radiant levels
    playerLevelUI[0][0] = ui->radiantLevel_1;
    playerLevelUI[0][1] = ui->radiantLevel_2;
    playerLevelUI[0][2] = ui->radiantLevel_3;
    playerLevelUI[0][3] = ui->radiantLevel_4;
    playerLevelUI[0][4] = ui->radiantLevel_5;
    //dire Levels
    playerLevelUI[1][0] = ui->direLevel_1;
    playerLevelUI[1][1] = ui->direLevel_2;
    playerLevelUI[1][2] = ui->direLevel_3;
    playerLevelUI[1][3] = ui->direLevel_4;
    playerLevelUI[1][4] = ui->direLevel_5;

    //radiant Hero Pic
    playerheroPicUI[0][0] = ui->radiantHeroPic_1;
    playerheroPicUI[0][1] = ui->radiantHeroPic_2;
    playerheroPicUI[0][2] = ui->radiantHeroPic_3;
    playerheroPicUI[0][3] = ui->radiantHeroPic_4;
    playerheroPicUI[0][4] = ui->radiantHeroPic_5;
    //dire hero pic
    playerheroPicUI[1][0] = ui->direHeroPic_1;
    playerheroPicUI[1][1] = ui->direHeroPic_2;
    playerheroPicUI[1][2] = ui->direHeroPic_3;
    playerheroPicUI[1][3] = ui->direHeroPic_4;
    playerheroPicUI[1][4] = ui->direHeroPic_5;

    //radiant hero name
    playerHeroNameUI[0][0] = ui->radiantHero_1;
    playerHeroNameUI[0][1] = ui->radiantHero_2;
    playerHeroNameUI[0][2] = ui->radiantHero_3;
    playerHeroNameUI[0][3] = ui->radiantHero_4;
    playerHeroNameUI[0][4] = ui->radiantHero_5;
    //dire hero name
    playerHeroNameUI[1][0] = ui->direHero_1;
    playerHeroNameUI[1][1] = ui->direHero_2;
    playerHeroNameUI[1][2] = ui->direHero_3;
    playerHeroNameUI[1][3] = ui->direHero_4;
    playerHeroNameUI[1][4] = ui->direHero_5;

    //radiant Kills
    playerKillsUI[0][0] = ui->radiantKills_1;
    playerKillsUI[0][1] = ui->radiantKills_2;
    playerKillsUI[0][2] = ui->radiantKills_3;
    playerKillsUI[0][3] = ui->radiantKills_4;
    playerKillsUI[0][4] = ui->radiantKills_5;
    //dire kills
    playerKillsUI[1][0] = ui->direKills_1;
    playerKillsUI[1][1] = ui->direKills_2;
    playerKillsUI[1][2] = ui->direKills_3;
    playerKillsUI[1][3] = ui->direKills_4;
    playerKillsUI[1][4] = ui->direKills_5;

    //radiant Deaths
    playerDeathsUI[0][0] = ui->radiantDeaths_1;
    playerDeathsUI[0][1] = ui->radiantDeaths_2;
    playerDeathsUI[0][2] = ui->radiantDeaths_3;
    playerDeathsUI[0][3] = ui->radiantDeaths_4;
    playerDeathsUI[0][4] = ui->radiantDeaths_5;
    //dire Deaths
    playerDeathsUI[1][0] = ui->direDeaths_1;
    playerDeathsUI[1][1] = ui->direDeaths_2;
    playerDeathsUI[1][2] = ui->direDeaths_3;
    playerDeathsUI[1][3] = ui->direDeaths_4;
    playerDeathsUI[1][4] = ui->direDeaths_5;

    //radiant Assists
    playerAssistsUI[0][0] = ui->radiantAssists_1;
    playerAssistsUI[0][1] = ui->radiantAssists_2;
    playerAssistsUI[0][2] = ui->radiantAssists_3;
    playerAssistsUI[0][3] = ui->radiantAssists_4;
    playerAssistsUI[0][4] = ui->radiantAssists_5;
    //dire Assists
    playerAssistsUI[1][0] = ui->direAssists_1;
    playerAssistsUI[1][1] = ui->direAssists_2;
    playerAssistsUI[1][2] = ui->direAssists_3;
    playerAssistsUI[1][3] = ui->direAssists_4;
    playerAssistsUI[1][4] = ui->direAssists_5;

    //radiant items 0-5
    //player 1
    playerItemsUI[0][0][0] = ui->radiantItems_1_1;
    playerItemsUI[0][0][1] = ui->radiantItems_1_2;
    playerItemsUI[0][0][2] = ui->radiantItems_1_3;
    playerItemsUI[0][0][3] = ui->radiantItems_1_4;
    playerItemsUI[0][0][4] = ui->radiantItems_1_5;
    playerItemsUI[0][0][5] = ui->radiantItems_1_6;
    //player 2
    playerItemsUI[0][1][0] = ui->radiantItems_2_1;
    playerItemsUI[0][1][1] = ui->radiantItems_2_2;
    playerItemsUI[0][1][2] = ui->radiantItems_2_3;
    playerItemsUI[0][1][3] = ui->radiantItems_2_4;
    playerItemsUI[0][1][4] = ui->radiantItems_2_5;
    playerItemsUI[0][1][5] = ui->radiantItems_2_6;
    //player 3
    playerItemsUI[0][2][0] = ui->radiantItems_3_1;
    playerItemsUI[0][2][1] = ui->radiantItems_3_2;
    playerItemsUI[0][2][2] = ui->radiantItems_3_3;
    playerItemsUI[0][2][3] = ui->radiantItems_3_4;
    playerItemsUI[0][2][4] = ui->radiantItems_3_5;
    playerItemsUI[0][2][5] = ui->radiantItems_3_6;
    //player 4
    playerItemsUI[0][3][0] = ui->radiantItems_4_1;
    playerItemsUI[0][3][1] = ui->radiantItems_4_2;
    playerItemsUI[0][3][2] = ui->radiantItems_4_3;
    playerItemsUI[0][3][3] = ui->radiantItems_4_4;
    playerItemsUI[0][3][4] = ui->radiantItems_4_5;
    playerItemsUI[0][3][5] = ui->radiantItems_4_6;
    //player 5
    playerItemsUI[0][4][0] = ui->radiantItems_5_1;
    playerItemsUI[0][4][1] = ui->radiantItems_5_2;
    playerItemsUI[0][4][2] = ui->radiantItems_5_3;
    playerItemsUI[0][4][3] = ui->radiantItems_5_4;
    playerItemsUI[0][4][4] = ui->radiantItems_5_5;
    playerItemsUI[0][4][5] = ui->radiantItems_5_6;
    //dire items
    //player 1
    playerItemsUI[1][0][0] = ui->direItems_1_1;
    playerItemsUI[1][0][1] = ui->direItems_1_2;
    playerItemsUI[1][0][2] = ui->direItems_1_3;
    playerItemsUI[1][0][3] = ui->direItems_1_4;
    playerItemsUI[1][0][4] = ui->direItems_1_5;
    playerItemsUI[1][0][5] = ui->direItems_1_6;
    //player 2
    playerItemsUI[1][1][0] = ui->direItems_2_1;
    playerItemsUI[1][1][1] = ui->direItems_2_2;
    playerItemsUI[1][1][2] = ui->direItems_2_3;
    playerItemsUI[1][1][3] = ui->direItems_2_4;
    playerItemsUI[1][1][4] = ui->direItems_2_5;
    playerItemsUI[1][1][5] = ui->direItems_2_6;
    //player 3
    playerItemsUI[1][2][0] = ui->direItems_3_1;
    playerItemsUI[1][2][1] = ui->direItems_3_1;
    playerItemsUI[1][2][2] = ui->direItems_3_1;
    playerItemsUI[1][2][3] = ui->direItems_3_1;
    playerItemsUI[1][2][4] = ui->direItems_3_1;
    playerItemsUI[1][2][5] = ui->direItems_3_1;
    //player 4
    playerItemsUI[1][3][0] = ui->direItems_4_1;
    playerItemsUI[1][3][1] = ui->direItems_4_1;
    playerItemsUI[1][3][2] = ui->direItems_4_1;
    playerItemsUI[1][3][3] = ui->direItems_4_1;
    playerItemsUI[1][3][4] = ui->direItems_4_1;
    playerItemsUI[1][3][5] = ui->direItems_4_1;
    //player 5
    playerItemsUI[1][4][0] = ui->direItems_5_1;
    playerItemsUI[1][4][1] = ui->direItems_5_1;
    playerItemsUI[1][4][2] = ui->direItems_5_1;
    playerItemsUI[1][4][3] = ui->direItems_5_1;
    playerItemsUI[1][4][4] = ui->direItems_5_1;
    playerItemsUI[1][4][5] = ui->direItems_5_1;

    //radiant Gold
    playerGoldUI[0][0] = ui->radiantGold_1;
    playerGoldUI[0][1] = ui->radiantGold_2;
    playerGoldUI[0][2] = ui->radiantGold_3;
    playerGoldUI[0][3] = ui->radiantGold_4;
    playerGoldUI[0][4] = ui->radiantGold_5;
    //dire Gold
    playerGoldUI[1][0] = ui->direGold_1;
    playerGoldUI[1][1] = ui->direGold_2;
    playerGoldUI[1][2] = ui->direGold_3;
    playerGoldUI[1][3] = ui->direGold_4;
    playerGoldUI[1][4] = ui->direGold_5;

    //radiant Last Hits
    playerLastHitsUI[0][0] = ui->radiantLH_1;
    playerLastHitsUI[0][1] = ui->radiantLH_2;
    playerLastHitsUI[0][2] = ui->radiantLH_3;
    playerLastHitsUI[0][3] = ui->radiantLH_4;
    playerLastHitsUI[0][4] = ui->radiantLH_5;
    //dire Last Hits
    playerLastHitsUI[1][0] = ui->direLH_1;
    playerLastHitsUI[1][1] = ui->direLH_2;
    playerLastHitsUI[1][2] = ui->direLH_3;
    playerLastHitsUI[1][3] = ui->direLH_4;
    playerLastHitsUI[1][4] = ui->direLH_5;

    //radiant Denies
    playerDeniesUI[0][0] = ui->radiantDN_1;
    playerDeniesUI[0][1] = ui->radiantDN_2;
    playerDeniesUI[0][2] = ui->radiantDN_3;
    playerDeniesUI[0][3] = ui->radiantDN_4;
    playerDeniesUI[0][4] = ui->radiantDN_5;
    //dire Denies
    playerDeniesUI[1][0] = ui->direDN_1;
    playerDeniesUI[1][1] = ui->direDN_2;
    playerDeniesUI[1][2] = ui->direDN_3;
    playerDeniesUI[1][3] = ui->direDN_4;
    playerDeniesUI[1][4] = ui->direDN_5;

    //radiant Gold/Min
    playerGPMUI[0][0] = ui->radiantGPM_1;
    playerGPMUI[0][1] = ui->radiantGPM_2;
    playerGPMUI[0][2] = ui->radiantGPM_3;
    playerGPMUI[0][3] = ui->radiantGPM_4;
    playerGPMUI[0][4] = ui->radiantGPM_5;
    //dire Gold/Min
    playerGPMUI[1][0] = ui->direGPM_1;
    playerGPMUI[1][1] = ui->direGPM_2;
    playerGPMUI[1][2] = ui->direGPM_3;
    playerGPMUI[1][3] = ui->direGPM_4;
    playerGPMUI[1][4] = ui->direGPM_5;

    //radiant XPM
    playerXPMUI[0][0] = ui->radiantXPM_1;
    playerXPMUI[0][1] = ui->radiantXPM_2;
    playerXPMUI[0][2] = ui->radiantXPM_3;
    playerXPMUI[0][3] = ui->radiantXPM_4;
    playerXPMUI[0][4] = ui->radiantXPM_5;
    //dire XPM
    playerXPMUI[1][0] = ui->direXPM_1;
    playerXPMUI[1][1] = ui->direXPM_2;
    playerXPMUI[1][2] = ui->direXPM_3;
    playerXPMUI[1][3] = ui->direXPM_4;
    playerXPMUI[1][4] = ui->direXPM_5;
}
