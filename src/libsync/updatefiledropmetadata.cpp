/*
 * Copyright (C) by Oleksandr Zolotov <alex@nextcloud.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "updatefiledropmetadata.h"

#include "clientsideencryptionjobs.h"
#include "networkjobs.h"
#include "clientsideencryption.h"
#include "syncfileitem.h"

namespace OCC {

Q_LOGGING_CATEGORY(lcUpdateFileDropMetadataJob, "nextcloud.sync.propagator.updatefiledropmetadatajon", QtInfoMsg)

}

namespace OCC {

UpdateFileDropMetadataJob::UpdateFileDropMetadataJob(OwncloudPropagator *propagator, const QString &path)
    : PropagatorJob(propagator)
    , _path(path)
{
}

void UpdateFileDropMetadataJob::start()
{
    /* If the file is in a encrypted folder, which we know, we wouldn't be here otherwise,
     * we need to do the long road:
     * find the ID of the folder.
     * lock the folder using it's id.
     * download the metadata
     * update the metadata
     * upload the file
     * upload the metadata
     * unlock the folder.
     */
    qCDebug(lcUpdateFileDropMetadataJob) << "Folder is encrypted, let's get the Id from it.";
    auto job = new LsColJob(propagator()->account(), _path, this);
    job->setProperties({"resourcetype", "http://owncloud.org/ns:fileid"});
    connect(job, &LsColJob::directoryListingSubfolders, this, &UpdateFileDropMetadataJob::slotFolderEncryptedIdReceived);
    connect(job, &LsColJob::finishedWithError, this, &UpdateFileDropMetadataJob::slotFolderEncryptedIdError);
    job->start();
}

bool UpdateFileDropMetadataJob::scheduleSelfOrChild()
{
    if (_state == Finished) {
        return false;
    }

    if (_state == NotYetStarted) {
        _state = Running;
        start();
    }

    return true;
}

PropagatorJob::JobParallelism UpdateFileDropMetadataJob::parallelism()
{
    return PropagatorJob::JobParallelism::WaitForFinished;
}

void UpdateFileDropMetadataJob::slotFolderEncryptedIdReceived(const QStringList &list)
{
    qCDebug(lcUpdateFileDropMetadataJob) << "Received id of folder, trying to lock it so we can prepare the metadata";
    auto job = qobject_cast<LsColJob *>(sender());
    const auto &folderInfo = job->_folderInfos.value(list.first());
    _folderLockFirstTry.start();
    slotTryLock(folderInfo.fileId);
}

void UpdateFileDropMetadataJob::slotTryLock(const QByteArray &fileId)
{
    auto *lockJob = new LockEncryptFolderApiJob(propagator()->account(), fileId, this);
    connect(lockJob, &LockEncryptFolderApiJob::success, this, &UpdateFileDropMetadataJob::slotFolderLockedSuccessfully);
    connect(lockJob, &LockEncryptFolderApiJob::error, this, &UpdateFileDropMetadataJob::slotFolderLockedError);
    lockJob->start();
}

void UpdateFileDropMetadataJob::slotFolderLockedSuccessfully(const QByteArray &fileId, const QByteArray &token)
{
    qCDebug(lcUpdateFileDropMetadataJob) << "Folder" << fileId << "Locked Successfully for Upload, Fetching Metadata";
    // Should I use a mutex here?
    _currentLockingInProgress = true;
    _folderToken = token;
    _folderId = fileId;
    _isFolderLocked = true;

    auto job = new GetMetadataApiJob(propagator()->account(), _folderId);
    connect(job, &GetMetadataApiJob::jsonReceived, this, &UpdateFileDropMetadataJob::slotFolderEncryptedMetadataReceived);
    connect(job, &GetMetadataApiJob::error, this, &UpdateFileDropMetadataJob::slotFolderEncryptedMetadataError);

    job->start();
}

void UpdateFileDropMetadataJob::slotFolderEncryptedMetadataError(const QByteArray &fileId, int httpReturnCode)
{
    Q_UNUSED(fileId);
    Q_UNUSED(httpReturnCode);
    qCDebug(lcUpdateFileDropMetadataJob()) << "Error Getting the encrypted metadata. Pretend we got empty metadata.";
    FolderMetadata emptyMetadata(propagator()->account());
    emptyMetadata.encryptedMetadata();
    auto json = QJsonDocument::fromJson(emptyMetadata.encryptedMetadata());
    slotFolderEncryptedMetadataReceived(json, httpReturnCode);
}

void UpdateFileDropMetadataJob::slotFolderEncryptedMetadataReceived(const QJsonDocument &json, int statusCode)
{
    qCDebug(lcUpdateFileDropMetadataJob) << "Metadata Received, Preparing it for the new file." << json.toVariant();

    // Encrypt File!
    _metadata = new FolderMetadata(propagator()->account(), json.toJson(QJsonDocument::Compact), statusCode);
    if (!_metadata->moveFileDropToFiles()) {
        unlockFolder();
        return;
    }

    auto job = new UpdateMetadataApiJob(propagator()->account(), _folderId, _metadata->encryptedMetadata(), _folderToken);

    connect(job, &UpdateMetadataApiJob::success, this, &UpdateFileDropMetadataJob::slotUpdateMetadataSuccess);
    connect(job, &UpdateMetadataApiJob::error, this, &UpdateFileDropMetadataJob::slotUpdateMetadataError);
    job->start();
}

void UpdateFileDropMetadataJob::slotUpdateMetadataSuccess(const QByteArray &fileId)
{
    Q_UNUSED(fileId);
    qCDebug(lcUpdateFileDropMetadataJob) << "Uploading of the metadata success, Encrypting the file";

    qCDebug(lcUpdateFileDropMetadataJob) << "Finalizing the upload part, now the actuall uploader will take over";
    unlockFolder();
}

void UpdateFileDropMetadataJob::slotUpdateMetadataError(const QByteArray &fileId, int httpErrorResponse)
{
    qCDebug(lcUpdateFileDropMetadataJob) << "Update metadata error for folder" << fileId << "with error" << httpErrorResponse;
    qCDebug(lcUpdateFileDropMetadataJob()) << "Unlocking the folder.";
    unlockFolder();
}

void UpdateFileDropMetadataJob::slotFolderLockedError(const QByteArray &fileId, int httpErrorCode)
{
    Q_UNUSED(httpErrorCode);
    qCDebug(lcUpdateFileDropMetadataJob) << "Folder" << fileId << "Coundn't be locked.";
    emit finished(SyncFileItem::Status::NormalError);
}

void UpdateFileDropMetadataJob::slotFolderEncryptedIdError(QNetworkReply *r)
{
    Q_UNUSED(r);
    qCDebug(lcUpdateFileDropMetadataJob) << "Error retrieving the Id of the encrypted folder.";
}

void UpdateFileDropMetadataJob::unlockFolder()
{
    ASSERT(!_isUnlockRunning);

    if (!_isFolderLocked) {
        emit finished(SyncFileItem::Status::Success);
        return;
    }

    if (_isUnlockRunning) {
        qWarning() << "Double-call to unlockFolder.";
        return;
    }

    _isUnlockRunning = true;

    qDebug() << "Calling Unlock";
    auto *unlockJob = new UnlockEncryptFolderApiJob(propagator()->account(), _folderId, _folderToken, this);

    connect(unlockJob, &UnlockEncryptFolderApiJob::success, [this](const QByteArray &folderId) {
        qDebug() << "Successfully Unlocked";
        _folderToken = "";
        _folderId = "";
        _isFolderLocked = false;

        emit folderUnlocked(folderId, 200);
        _isUnlockRunning = false;
        emit finished(SyncFileItem::Status::Success);
    });
    connect(unlockJob, &UnlockEncryptFolderApiJob::error, [this](const QByteArray &folderId, int httpStatus) {
        qDebug() << "Unlock Error";

        emit folderUnlocked(folderId, httpStatus);
        _isUnlockRunning = false;
        emit finished(SyncFileItem::Status::NormalError);
    });
    unlockJob->start();
}


}
