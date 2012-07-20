// cratefeature.cpp
// Created 10/22/2009 by RJ Ryan (rryan@mit.edu)

#include <QInputDialog>
#include <QMenu>
#include <QLineEdit>

#include "library/cratefeature.h"
#include "library/parser.h"
#include "library/parserm3u.h"
#include "library/parserpls.h"
#include "library/parsercsv.h"

#include "library/cratetablemodel.h"
#include "library/trackcollection.h"
#include "widget/wlibrarytextbrowser.h"
#include "widget/wlibrary.h"
#include "widget/wlibrarysidebar.h"
#include "mixxxkeyboard.h"
#include "treeitem.h"
#include "soundsourceproxy.h"

CrateFeature::CrateFeature(QObject* parent,
                           TrackCollection* pTrackCollection, ConfigObject<ConfigValue>* pConfig)
        : m_pTrackCollection(pTrackCollection),
          m_crateDao(pTrackCollection->getCrateDAO()),
          m_crateListTableModel(this, pTrackCollection->getDatabase()),
          m_crateTableModel(this, pTrackCollection),
          m_pConfig(pConfig) {
    Q_UNUSED(parent);
    m_pCreateCrateAction = new QAction(tr("New Crate"),this);
    connect(m_pCreateCrateAction, SIGNAL(triggered()),
            this, SLOT(slotCreateCrate()));

    m_pDeleteCrateAction = new QAction(tr("Remove"),this);
    connect(m_pDeleteCrateAction, SIGNAL(triggered()),
            this, SLOT(slotDeleteCrate()));

    m_pRenameCrateAction = new QAction(tr("Rename"),this);
    connect(m_pRenameCrateAction, SIGNAL(triggered()),
            this, SLOT(slotRenameCrate()));

    m_pLockCrateAction = new QAction(tr("Lock"),this);
    connect(m_pLockCrateAction, SIGNAL(triggered()),
            this, SLOT(slotToggleCrateLock()));

    m_pImportPlaylistAction = new QAction(tr("Import Crate"),this);
    connect(m_pImportPlaylistAction, SIGNAL(triggered()),
            this, SLOT(slotImportPlaylist()));
    m_pExportPlaylistAction = new QAction(tr("Export Crate"), this);
    connect(m_pExportPlaylistAction, SIGNAL(triggered()),
            this, SLOT(slotExportPlaylist()));

    connect(&m_crateDao, SIGNAL(added(int)),
            this, SLOT(slotCrateTableChanged(int)));

    connect(&m_crateDao, SIGNAL(deleted(int)),
            this, SLOT(slotCrateTableChanged(int)));

    connect(&m_crateDao, SIGNAL(renamed(int)),
            this, SLOT(slotCrateTableChanged(int)));

    connect(&m_crateDao, SIGNAL(lockChanged(int)),
            this, SLOT(slotCrateTableChanged(int)));


    m_crateListTableModel.setTable("crates");
    m_crateListTableModel.setSort(m_crateListTableModel.fieldIndex("name"),
                                  Qt::AscendingOrder);
    m_crateListTableModel.setFilter("show = 1");
    m_crateListTableModel.select();

    // construct child model
    TreeItem *rootItem = new TreeItem();
    m_childModel.setRootItem(rootItem);
    constructChildModel(-1);
}

CrateFeature::~CrateFeature() {
    //delete QActions
    delete m_pCreateCrateAction;
    delete m_pDeleteCrateAction;
    delete m_pRenameCrateAction;
    delete m_pLockCrateAction;
    delete m_pImportPlaylistAction;
}

QVariant CrateFeature::title() {
    return tr("Crates");
}

QIcon CrateFeature::getIcon() {
    return QIcon(":/images/library/ic_library_crates.png");
}

bool CrateFeature::dropAccept(QUrl url) {
    Q_UNUSED(url)
    return false;
}

bool CrateFeature::dropAcceptChild(const QModelIndex& index, QUrl url) {
    QString crateName = index.data().toString();
    int crateId = m_crateDao.getCrateIdByName(crateName);

    //XXX: See the comment in PlaylistFeature::dropAcceptChild() about
    //     QUrl::toLocalFile() vs. QUrl::toString() usage.
    QFileInfo file(url.toLocalFile());

    // Adds track, does not insert duplicates, handles unremoving logic.
    int trackId = m_pTrackCollection->getTrackDAO().addTrack(file, true);

    qDebug() << "CrateFeature::dropAcceptChild adding track"
             << trackId << "to crate" << crateId;

    if (trackId >= 0)
        return m_crateDao.addTrackToCrate(trackId, crateId);
    return false;
}

bool CrateFeature::dragMoveAccept(QUrl url) {
    Q_UNUSED(url)
    return false;
}

bool CrateFeature::dragMoveAcceptChild(const QModelIndex& index, QUrl url) {
    //TODO: Filter by supported formats regex and reject anything that doesn't match.
    QString crateName = index.data().toString();
    int crateId = m_crateDao.getCrateIdByName(crateName);
    bool locked = m_crateDao.isCrateLocked(crateId);

    QFileInfo file(url.toLocalFile());
    bool formatSupported = SoundSourceProxy::isFilenameSupported(file.fileName());
    return !locked && formatSupported;
}

void CrateFeature::bindWidget(WLibrarySidebar* sidebarWidget,
                              WLibrary* libraryWidget,
                              MixxxKeyboard* keyboard) {
    Q_UNUSED(sidebarWidget);
    Q_UNUSED(keyboard);
    WLibraryTextBrowser* edit = new WLibraryTextBrowser(libraryWidget);
    edit->setHtml(getRootViewHtml());
    edit->setOpenLinks(false);
    connect(edit,SIGNAL(anchorClicked(const QUrl)),
        this,SLOT(htmlLinkClicked(const QUrl))
    );
    libraryWidget->registerView("CRATEHOME", edit);
}

TreeItemModel* CrateFeature::getChildModel() {
    return &m_childModel;
}

void CrateFeature::activate() {
    emit(switchToView("CRATEHOME"));
    emit(restoreSearch(QString())); //disable search on crate home
}

void CrateFeature::activateChild(const QModelIndex& index) {
    if (!index.isValid())
        return;
    QString crateName = index.data().toString();
    int crateId = m_crateDao.getCrateIdByName(crateName);
    m_crateTableModel.setCrate(crateId);
    emit(showTrackModel(&m_crateTableModel));
}

void CrateFeature::onRightClick(const QPoint& globalPos) {
    m_lastRightClickedIndex = QModelIndex();
    QMenu menu(NULL);
    menu.addAction(m_pCreateCrateAction);
    menu.exec(globalPos);
}

void CrateFeature::onRightClickChild(const QPoint& globalPos, QModelIndex index) {
    //Save the model index so we can get it in the action slots...
    m_lastRightClickedIndex = index;

    QString crateName = index.data().toString();
    int crateId = m_crateDao.getCrateIdByName(crateName);

    bool locked = m_crateDao.isCrateLocked(crateId);

    m_pDeleteCrateAction->setEnabled(!locked);
    m_pRenameCrateAction->setEnabled(!locked);

    m_pLockCrateAction->setText(locked ? tr("Unlock") : tr("Lock"));

    QMenu menu(NULL);
    menu.addAction(m_pCreateCrateAction);
    menu.addSeparator();
    menu.addAction(m_pRenameCrateAction);
    menu.addAction(m_pDeleteCrateAction);
    menu.addAction(m_pLockCrateAction);
    menu.addSeparator();
    menu.addAction(m_pImportPlaylistAction);
    menu.addAction(m_pExportPlaylistAction);
    menu.exec(globalPos);
}

void CrateFeature::slotCreateCrate() {

    QString name;
    bool validNameGiven = false;

    do {
        bool ok = false;
        name = QInputDialog::getText(NULL,
                                     tr("New Crate"),
                                     tr("Crate name:"),
                                     QLineEdit::Normal, tr("New Crate"),
                                     &ok).trimmed();

        if (!ok)
            return;

        int existingId = m_crateDao.getCrateIdByName(name);

        if (existingId != -1) {
            QMessageBox::warning(NULL,
                                 tr("Creating Crate Failed"),
                                 tr("A crate by that name already exists."));
        }
        else if (name.isEmpty()) {
            QMessageBox::warning(NULL,
                                 tr("Creating Crate Failed"),
                                 tr("A crate cannot have a blank name."));
        }
        else {
            validNameGiven = true;
        }

    } while (!validNameGiven);

    int crate_id = m_crateDao.createCrate(name);

    if (crate_id != -1) {
        emit(showTrackModel(&m_crateTableModel));
    } else {
        qDebug() << "Error creating crate with name " << name;
        QMessageBox::warning(NULL,
                             tr("Creating Crate Failed"),
                             tr("An unknown error occurred while creating crate: ")
                             + name);

    }
}

void CrateFeature::slotDeleteCrate() {
    QString crateName = m_lastRightClickedIndex.data().toString();
    int crateId = m_crateDao.getCrateIdByName(crateName);
    bool locked = m_crateDao.isCrateLocked(crateId);

    if (locked) {
        qDebug() << "Skipping crate deletion because crate" << crateId << "is locked.";
        return;
    }

    bool deleted = m_crateDao.deleteCrate(crateId);

    if (deleted) {;
        activate();
    } else {
        qDebug() << "Failed to delete crateId" << crateId;
    }
}

void CrateFeature::slotRenameCrate() {
    QString oldName = m_lastRightClickedIndex.data().toString();
    int crateId = m_crateDao.getCrateIdByName(oldName);
    bool locked = m_crateDao.isCrateLocked(crateId);

    if (locked) {
        qDebug() << "Skipping crate rename because crate" << crateId << "is locked.";
        return;
    }

    QString newName;
    bool validNameGiven = false;

    do {
        bool ok = false;
        newName = QInputDialog::getText(NULL,
                                        tr("Rename Crate"),
                                        tr("New crate name:"),
                                        QLineEdit::Normal,
                                        oldName,
                                        &ok).trimmed();

        if (!ok || newName == oldName) {
            return;
        }

        int existingId = m_crateDao.getCrateIdByName(newName);

        if (existingId != -1) {
            QMessageBox::warning(NULL,
                                tr("Renaming Crate Failed"),
                                tr("A crate by that name already exists."));
        }
        else if (newName.isEmpty()) {
            QMessageBox::warning(NULL,
                                tr("Renaming Crate Failed"),
                                tr("A crate cannot have a blank name."));
        }
        else {
            validNameGiven = true;
        }
    } while (!validNameGiven);


    if (!m_crateDao.renameCrate(crateId, newName)) {
        qDebug() << "Failed to rename crateId" << crateId;
    }
}

void CrateFeature::slotToggleCrateLock()
{
    QString crateName = m_lastRightClickedIndex.data().toString();
    int crateId = m_crateDao.getCrateIdByName(crateName);
    bool locked = !m_crateDao.isCrateLocked(crateId);

    if (!m_crateDao.setCrateLocked(crateId, locked)) {
        qDebug() << "Failed to toggle lock of crateId " << crateId;
    }
}


/**
  * Purpose: When inserting or removing playlists,
  * we require the sidebar model not to reset.
  * This method queries the database and does dynamic insertion
*/
QModelIndex CrateFeature::constructChildModel(int selected_id)
{
    QList<TreeItem*> data_list;
    int nameColumn = m_crateListTableModel.record().indexOf("name");
    int idColumn = m_crateListTableModel.record().indexOf("id");
    int selected_row = -1;
    // Access the invisible root item
    TreeItem* root = m_childModel.getItem(QModelIndex());

    for (int row = 0; row < m_crateListTableModel.rowCount(); ++row) {
            QModelIndex ind = m_crateListTableModel.index(row, nameColumn);
            QString crate_name = m_crateListTableModel.data(ind).toString();
            ind = m_crateListTableModel.index(row, idColumn);
            int crate_id = m_crateListTableModel.data(ind).toInt();

            if (selected_id == crate_id) {
                // save index for selection
                selected_row = row;  m_childModel.index(selected_row, 0);
            }

            // Create the TreeItem whose parent is the invisible root item
            TreeItem* item = new TreeItem(crate_name, crate_name, this, root);
            bool locked = m_crateDao.isCrateLocked(crate_id);
            item->setIcon(locked ? QIcon(":/images/library/ic_library_locked.png") : QIcon());
            data_list.append(item);
    }
    // Append all the newly created TreeItems in a dynamic way to the childmodel
    m_childModel.insertRows(data_list, 0, m_crateListTableModel.rowCount());
    if (selected_row == -1) {
        return QModelIndex();
    }
    return m_childModel.index(selected_row, 0);
}

/**
  * Clears the child model dynamically
  */
void CrateFeature::clearChildModel()
{
    m_childModel.removeRows(0,m_crateListTableModel.rowCount());
}

void CrateFeature::slotImportPlaylist()
{
    qDebug() << "slotImportPlaylist() row:" ; //<< m_lastRightClickedIndex.data();


    QString playlist_file = QFileDialog::getOpenFileName(
        NULL,
        tr("Import Playlist"),
        QDesktopServices::storageLocation(QDesktopServices::MusicLocation),
        tr("Playlist Files (*.m3u *.m3u8 *.pls *.csv)"));
    // Exit method if user cancelled the open dialog.
    if (playlist_file.isNull() || playlist_file.isEmpty() ) return;

    Parser* playlist_parser = NULL;

    if (playlist_file.endsWith(".m3u", Qt::CaseInsensitive) ||
        playlist_file.endsWith(".m3u8", Qt::CaseInsensitive)) {
        // .m3u8 is Utf8 representation of an m3u playlist
        playlist_parser = new ParserM3u();
    } else if (playlist_file.endsWith(".pls", Qt::CaseInsensitive)) {
        playlist_parser = new ParserPls();
    } else if (playlist_file.endsWith(".csv", Qt::CaseInsensitive)) {
        playlist_parser = new ParserCsv();
    } else {
        return;
    }

    QList<QString> entries = playlist_parser->parse(playlist_file);
    //qDebug() << "Size of Imported Playlist: " << entries.size();

    //Iterate over the List that holds URLs of playlist entires
    for (int i = 0; i < entries.size(); ++i) {
        m_crateTableModel.addTrack(QModelIndex(), entries[i]);
        //qDebug() << "Playlist entry: " << entries[i];
    }

    //delete the parser object
    if(playlist_parser)
        delete playlist_parser;
}

void CrateFeature::onLazyChildExpandation(const QModelIndex &index){
    Q_UNUSED(index);
    //Nothing to do because the childmodel is not of lazy nature.
}

void CrateFeature::slotExportPlaylist(){
    qDebug() << "Export crate" << m_lastRightClickedIndex.data();
    QString file_location = QFileDialog::getSaveFileName(
        NULL,
        tr("Export Crate"),
        QDesktopServices::storageLocation(QDesktopServices::MusicLocation),
        tr("M3U Playlist (*.m3u);;M3U8 Playlist (*.m3u8);;PLS Playlist (*.pls);;Text CSV (*.csv);;Readable Text (*.txt)"));
    // Exit method if user cancelled the open dialog.
    if (file_location.isNull() || file_location.isEmpty()) {
        return;
    }
    // check config if relative paths are desired
    bool useRelativePath = static_cast<bool>(
        m_pConfig->getValueString(
            ConfigKey("[Library]", "UseRelativePathOnExport")).toInt());

    // Create list of files of the crate
    QList<QString> playlist_items;
    // Create a new table model since the main one might have an active search.
    QScopedPointer<CrateTableModel> pCrateTableModel(
        new CrateTableModel(this, m_pTrackCollection));
    pCrateTableModel->setCrate(m_crateTableModel.getCrate());
    pCrateTableModel->select();

    if (file_location.endsWith(".csv", Qt::CaseInsensitive)) {
            ParserCsv::writeCSVFile(file_location, pCrateTableModel.data(), useRelativePath);
    } else if (file_location.endsWith(".txt", Qt::CaseInsensitive)) {
             ParserCsv::writeReadableTextFile(file_location, pCrateTableModel.data(), false);
    } else{
        // populate a list of files of the crate
        QList<QString> playlist_items;
        int rows = pCrateTableModel->rowCount();
        for (int i = 0; i < rows; ++i) {
            QModelIndex index = m_crateTableModel.index(i, 0);
            playlist_items << m_crateTableModel.getTrackLocation(index);
        }

        if (file_location.endsWith(".pls", Qt::CaseInsensitive)) {
            ParserPls::writePLSFile(file_location, playlist_items, useRelativePath);
        } else if (file_location.endsWith(".m3u8", Qt::CaseInsensitive)) {
            ParserM3u::writeM3U8File(file_location, playlist_items, useRelativePath);
        } else {
            //default export to M3U if file extension is missing
            if(!file_location.endsWith(".m3u", Qt::CaseInsensitive))
            {
                qDebug() << "Crate export: No valid file extension specified. Appending .m3u "
                         << "and exporting to M3U.";
                file_location.append(".m3u");
            }
            ParserM3u::writeM3UFile(file_location, playlist_items, useRelativePath);
        }
    }
}

void CrateFeature::slotCrateTableChanged(int crateId) {
    //qDebug() << "slotPlaylistTableChanged() playlistId:" << playlistId;
    clearChildModel();
    m_crateListTableModel.select();
    m_lastRightClickedIndex = constructChildModel(crateId);
    // Switch the view to the crate.
    m_crateTableModel.setCrate(crateId);
    // Update selection
    emit(featureSelect(this, m_lastRightClickedIndex));
}

void CrateFeature::htmlLinkClicked(const QUrl & link) {
    if (QString(link.path())=="create") {
        slotCreateCrate();
    } else {
        qDebug() << "Unknown crate link clicked" << link;
    }
}

QString CrateFeature::getRootViewHtml() const {
    QString cratesTitle = tr("Crates");
    QString cratesSummary = tr("Crates are a great way to help organize the music you want to DJ with.");
    QString cratesSummary2 = tr("Make a crate for your next gig, for your favorite electrohouse tracks, or for your most requested songs.");
    QString cratesSummary3 = tr("Crates let you organize your music however you'd like!");

    QString html;
    QString createCrateLink = tr("Create new crate");
    html.append(QString("<h2>%1</h2>").arg(cratesTitle));
    html.append("<table border=\"0\" cellpadding=\"5\"><tr><td>");
    html.append(QString("<p>%1</p>").arg(cratesSummary));
    html.append(QString("<p>%1</p>").arg(cratesSummary2));
    html.append(QString("<p>%1</p>").arg(cratesSummary3));
    html.append("</td><td rowspan=\"2\">");
    html.append("<img src=\"qrc:/images/library/crates_art.png\">");
    html.append("</td></tr>");
    html.append(
        QString("<tr><td><a href=\"create\">%1</a>").arg(createCrateLink)
    );
    html.append("</td></tr></table>");
    return html;
}
