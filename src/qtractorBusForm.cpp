// qtractorBusForm.cpp
//
/****************************************************************************
   Copyright (C) 2005-2009, rncbc aka Rui Nuno Capela. All rights reserved.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License along
   with this program; if not, write to the Free Software Foundation, Inc.,
   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

*****************************************************************************/

#include "qtractorBusForm.h"

#include "qtractorAbout.h"
#include "qtractorOptions.h"
#include "qtractorEngineCommand.h"
#include "qtractorAudioEngine.h"
#include "qtractorMidiEngine.h"

#include "qtractorPlugin.h"

#include <QHeaderView>
#include <QMessageBox>
#include <QMenu>

#if QT_VERSION < 0x040300
#define lighter(x)	light(x)
#define darker(x)	dark(x)
#endif


//----------------------------------------------------------------------
// class qtractorBusListItem -- Custom bus listview item.
//

class qtractorBusListItem : public QTreeWidgetItem
{
public:

	// Constructor.
	qtractorBusListItem(QTreeWidgetItem *pRootItem, qtractorBus *pBus)
		: QTreeWidgetItem(pRootItem), m_pBus(pBus)
	{
		switch (m_pBus->busType()) {
		case qtractorTrack::Audio:
			QTreeWidgetItem::setIcon(0, QIcon(":/icons/trackAudio.png"));
			break;
		case qtractorTrack::Midi:
			QTreeWidgetItem::setIcon(0, QIcon(":/icons/trackMidi.png"));
			break;
		case qtractorTrack::None:
		default:
			break;
		}
		QTreeWidgetItem::setText(0, m_pBus->busName());
	}

	// Bus accessors.
	qtractorBus *bus() const { return m_pBus; }

private:

	// Instance variables.
	qtractorBus *m_pBus;
};


//----------------------------------------------------------------------------
// qtractorBusForm -- UI wrapper form.

// Constructor.
qtractorBusForm::qtractorBusForm (
	QWidget *pParent, Qt::WindowFlags wflags )
	: QDialog(pParent, wflags)
{
	// Setup UI struct...
	m_ui.setupUi(this);

	// Initialize locals.
	m_pBus        = NULL;
	m_pAudioRoot  = NULL;
	m_pMidiRoot   = NULL;
	m_iDirtySetup = 0;
	m_iDirtyCount = 0;
	m_iDirtyTotal = 0;

	QHeaderView *pHeader = m_ui.BusListView->header();
	pHeader->setResizeMode(QHeaderView::Custom);
	pHeader->setDefaultAlignment(Qt::AlignLeft);
	pHeader->setMovable(false);

	m_ui.BusListView->setContextMenuPolicy(Qt::CustomContextMenu);

	const QColor& rgbDark = palette().dark().color().darker(150);
	m_ui.BusTitleTextLabel->setPalette(QPalette(rgbDark));
	m_ui.BusTitleTextLabel->setAutoFillBackground(true);

	// (Re)initial contents.
	refreshBuses();

	// Try to restore normal window positioning.
	adjustSize();

	// UI signal/slot connections...
	QObject::connect(m_ui.BusListView,
		SIGNAL(currentItemChanged(QTreeWidgetItem*,QTreeWidgetItem*)),
		SLOT(selectBus()));
	QObject::connect(m_ui.BusListView,
		SIGNAL(customContextMenuRequested(const QPoint&)),
		SLOT(contextMenu(const QPoint&)));
	QObject::connect(m_ui.BusNameLineEdit,
		SIGNAL(textChanged(const QString&)),
		SLOT(changed()));
	QObject::connect(m_ui.BusModeComboBox,
		SIGNAL(activated(int)),
		SLOT(changed()));
	QObject::connect(m_ui.PassthruCheckBox,
		SIGNAL(clicked()),
		SLOT(changed()));
	QObject::connect(m_ui.AudioChannelsSpinBox,
		SIGNAL(valueChanged(int)),
		SLOT(changed()));
	QObject::connect(m_ui.AudioAutoConnectCheckBox,
		SIGNAL(clicked()),
		SLOT(changed()));

	QObject::connect(m_ui.InputPluginListView,
		SIGNAL(currentRowChanged(int)),
		SLOT(stabilizeForm()));
	QObject::connect(m_ui.AddInputPluginToolButton,
		SIGNAL(clicked()),
		SLOT(addInputPlugin()));
	QObject::connect(m_ui.RemoveInputPluginToolButton,
		SIGNAL(clicked()),
		SLOT(removeInputPlugin()));
	QObject::connect(m_ui.MoveUpInputPluginToolButton,
		SIGNAL(clicked()),
		SLOT(moveUpInputPlugin()));
	QObject::connect(m_ui.MoveDownInputPluginToolButton,
		SIGNAL(clicked()),
		SLOT(moveDownInputPlugin()));

	QObject::connect(m_ui.OutputPluginListView,
		SIGNAL(currentRowChanged(int)),
		SLOT(stabilizeForm()));
	QObject::connect(m_ui.AddOutputPluginToolButton,
		SIGNAL(clicked()),
		SLOT(addOutputPlugin()));
	QObject::connect(m_ui.RemoveOutputPluginToolButton,
		SIGNAL(clicked()),
		SLOT(removeOutputPlugin()));
	QObject::connect(m_ui.MoveUpOutputPluginToolButton,
		SIGNAL(clicked()),
		SLOT(moveUpOutputPlugin()));
	QObject::connect(m_ui.MoveDownOutputPluginToolButton,
		SIGNAL(clicked()),
		SLOT(moveDownOutputPlugin()));

	QObject::connect(m_ui.RefreshPushButton,
		SIGNAL(clicked()),
		SLOT(refreshBuses()));
	QObject::connect(m_ui.CreatePushButton,
		SIGNAL(clicked()),
		SLOT(createBus()));
	QObject::connect(m_ui.UpdatePushButton,
		SIGNAL(clicked()),
		SLOT(updateBus()));
	QObject::connect(m_ui.DeletePushButton,
		SIGNAL(clicked()),
		SLOT(deleteBus()));
	QObject::connect(m_ui.ClosePushButton,
		SIGNAL(clicked()),
		SLOT(reject()));

	stabilizeForm();
}


// Set current bus.
void qtractorBusForm::setBus ( qtractorBus *pBus )
{
	// Get the device view root item...
	QTreeWidgetItem *pRootItem = NULL;
	if (pBus) {
		switch (pBus->busType()) {
		case qtractorTrack::Audio:
			pRootItem = m_pAudioRoot;
			break;
		case qtractorTrack::Midi:
			pRootItem = m_pMidiRoot;
			break;
		default:
			break;
		}
	}
	// Is the root present?
	if (pRootItem == NULL) {
		stabilizeForm();
		return;
	}

	// For each child, test for identity...
	int iChildCount = pRootItem->childCount();
	for (int i = 0; i < iChildCount; i++) {
		QTreeWidgetItem *pItem = pRootItem->child(i);
		// If identities match, select as current device item.
		qtractorBusListItem *pBusItem
			= static_cast<qtractorBusListItem *> (pItem);
		if (pBusItem && pBusItem->bus() == pBus) {
			m_ui.BusListView->setCurrentItem(pItem);
			break;
		}
	}
}


// Current bus accessor.
qtractorBus *qtractorBusForm::bus (void)
{
	return m_pBus;
}


// Current bus accessor.
bool qtractorBusForm::isDirty (void)
{
	return (m_iDirtyTotal > 0);
}


// Show current selected bus.
void qtractorBusForm::showBus ( qtractorBus *pBus )
{
	m_iDirtySetup++;

	// Reset plugin lists...
	m_ui.InputPluginListView->setPluginList(NULL);
	m_ui.OutputPluginListView->setPluginList(NULL);

	// Settle current bus reference...
	m_pBus = pBus;

	// Show bus properties into view pane...
	if (pBus) {
		QString sBusTitle = pBus->busName();
		if (!sBusTitle.isEmpty())
			sBusTitle += " - ";
		switch (pBus->busType()) {
		case qtractorTrack::Audio:
		{
			sBusTitle += tr("Audio");
			qtractorAudioBus *pAudioBus
				= static_cast<qtractorAudioBus *> (pBus);
			if (pAudioBus) {
				m_ui.AudioChannelsSpinBox->setValue(
					pAudioBus->channels());
				m_ui.AudioAutoConnectCheckBox->setChecked(
					pAudioBus->isAutoConnect());
				// Set plugin lists...
				if (pAudioBus->busMode() & qtractorBus::Input)
					m_ui.InputPluginListView->setPluginList(
						pAudioBus->pluginList_in());
				if (pAudioBus->busMode() & qtractorBus::Output)
					m_ui.OutputPluginListView->setPluginList(
						pAudioBus->pluginList_out());
			}
			break;
		}
		case qtractorTrack::Midi:
		{
			sBusTitle += tr("MIDI");
			qtractorMidiBus *pMidiBus
				= static_cast<qtractorMidiBus *> (pBus);
			if (pMidiBus) {
				// Set plugin lists...
				if (pMidiBus->busMode() & qtractorBus::Input)
					m_ui.InputPluginListView->setPluginList(
						pMidiBus->pluginList_in());
				if (pMidiBus->busMode() & qtractorBus::Output)
					m_ui.OutputPluginListView->setPluginList(
						pMidiBus->pluginList_out());
			}
			break;
		}
		case qtractorTrack::None:
		default:
			break;
		}
		if (!sBusTitle.isEmpty())
			sBusTitle += ' ';
		m_ui.BusTitleTextLabel->setText(sBusTitle + tr("Bus"));
		m_ui.BusNameLineEdit->setText(pBus->busName());
		m_ui.BusModeComboBox->setCurrentIndex(int(pBus->busMode()) - 1);
		m_ui.PassthruCheckBox->setChecked(pBus->isPassthru());
	}

	// Reset dirty flag...
	m_iDirtyCount = 0;	
	m_iDirtySetup--;

	// Done.
	stabilizeForm();
}


// Refresh all buses list and views.
void qtractorBusForm::refreshBuses (void)
{
	//
	// (Re)Load complete bus listing ...
	//
	m_pAudioRoot = NULL;
	m_pMidiRoot  = NULL;
	m_ui.BusListView->clear();

	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession == NULL)
		return;

	// Audio buses...
	qtractorAudioEngine *pAudioEngine = pSession->audioEngine();
	if (pAudioEngine) {
		m_pAudioRoot = new QTreeWidgetItem(m_ui.BusListView);
		m_pAudioRoot->setText(0, ' ' + tr("Audio"));
		m_pAudioRoot->setFlags(	// Audio root item is not selectable...
			m_pAudioRoot->flags() & ~Qt::ItemIsSelectable);
		for (qtractorBus *pBus = pAudioEngine->buses().first();
				pBus; pBus = pBus->next())
			new qtractorBusListItem(m_pAudioRoot, pBus);
#if QT_VERSION >= 0x040201
		m_pAudioRoot->setExpanded(true);
#else
		m_ui.BusListView->setItemExpanded(m_pAudioRoot, true);
#endif
	}

	// MIDI buses...
	qtractorMidiEngine *pMidiEngine = pSession->midiEngine();
	if (pMidiEngine) {
		m_pMidiRoot = new QTreeWidgetItem(m_ui.BusListView);
		m_pMidiRoot->setText(0, ' ' + tr("MIDI"));
		m_pMidiRoot->setFlags(	// MIDI root item is not selectable...
			m_pMidiRoot->flags() & ~Qt::ItemIsSelectable);
		for (qtractorBus *pBus = pMidiEngine->buses().first();
				pBus; pBus = pBus->next())
			new qtractorBusListItem(m_pMidiRoot, pBus);
#if QT_VERSION >= 0x040201
		m_pMidiRoot->setExpanded(true);
#else
		m_ui.BusListView->setItemExpanded(m_pMidiRoot, true);
#endif
	}
}


// Bus selection slot.
void qtractorBusForm::selectBus (void)
{
	// Get current selected item, must not be a root one...
	QTreeWidgetItem *pItem = m_ui.BusListView->currentItem();
	if (pItem == NULL)
		return;
	if (pItem->parent() == NULL)
		return;

	// Just make it in current view...
	qtractorBusListItem *pBusItem
		= static_cast<qtractorBusListItem *> (pItem);
	if (pBusItem == NULL)
		return;

	// Check if we need an update?...
	qtractorBus *pBus = pBusItem->bus();
	if (m_pBus && m_pBus != pBus && m_iDirtyCount > 0) {
		switch (QMessageBox::warning(this,
			tr("Warning") + " - " QTRACTOR_TITLE,
			tr("Some settings have been changed.\n\n"
			"Do you want to apply the changes?"),
			QMessageBox::Apply |
			QMessageBox::Discard |
			QMessageBox::Cancel)) {
		case QMessageBox::Apply:
			if (updateBusEx(m_pBus)) {
				m_iDirtyTotal++;
				refreshBuses();
			}
			// Fall thru...
		case QMessageBox::Discard:
			break;;
		default:    // Cancel.
			return;
		}
	}

	// Get new one into view...
	showBus(pBus);
}


// Check whether the current view is elligible as a new bus.
bool qtractorBusForm::canCreateBus (void) const
{
	if (m_iDirtyCount == 0)
		return false;
	if (m_pBus == NULL)
		return false;

	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession == NULL)
		return false;

	const QString sBusName = m_ui.BusNameLineEdit->text().simplified();
	if (sBusName.isEmpty())
		return false;

	// Get the device view root item...
	qtractorEngine *pEngine = NULL;
	switch (m_pBus->busType()) {
	case qtractorTrack::Audio:
		pEngine = pSession->audioEngine();
		break;
	case qtractorTrack::Midi:
		pEngine = pSession->midiEngine();
		break;
	default:
		break;
	}
	// Is it still valid?
	if (pEngine == NULL)
		return false;

	// Is there one already?
	return (pEngine->findBus(sBusName) == NULL);
}


// Check whether the current view is elligible for update.
bool qtractorBusForm::canUpdateBus (void) const
{
	if (m_iDirtyCount == 0)
		return false;
	if (m_pBus == NULL)
		return false;

	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession == NULL)
		return false;

	const QString sBusName = m_ui.BusNameLineEdit->text().simplified();
	if (sBusName.isEmpty())
		return false;

	// Master (default) buses must be duplex...
	return (m_pBus->prev() || m_ui.BusModeComboBox->currentIndex() == 2);
}


// Check whether the current view is elligible for deletion.
bool qtractorBusForm::canDeleteBus (void) const
{
	if (m_pBus == NULL)
		return false;

	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession == NULL)
		return false;

	// The very first bus is never deletable...
	return (m_pBus->prev() != NULL);
}


// Update bus method.
bool qtractorBusForm::updateBusEx ( qtractorBus *pBus ) const
{
	if (pBus == NULL)
		return false;

	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession == NULL)
		return false;

	const QString sBusName = m_ui.BusNameLineEdit->text().simplified();
	if (sBusName.isEmpty())
		return false;

	// Reset plugin lists...
	m_ui.InputPluginListView->setPluginList(NULL);
	m_ui.OutputPluginListView->setPluginList(NULL);

	qtractorBus::BusMode busMode = qtractorBus::None;
	switch (m_ui.BusModeComboBox->currentIndex()) {
	case 0:
		busMode = qtractorBus::Input;
		break;
	case 1:
		busMode = qtractorBus::Output;
		break;
	case 2:
		busMode = qtractorBus::Duplex;
		break;
	}

	// Make it as an unduable command...
	qtractorUpdateBusCommand *pUpdateBusCommand
		= new qtractorUpdateBusCommand(pBus);

	// Set all updated properties...
	qtractorTrack::TrackType busType = pBus->busType();
	pUpdateBusCommand->setBusType(busType);
	pUpdateBusCommand->setBusName(sBusName);
	pUpdateBusCommand->setBusMode(busMode);
	pUpdateBusCommand->setPassthru(
		(busMode & qtractorBus::Duplex) == qtractorBus::Duplex
		&& m_ui.PassthruCheckBox->isChecked());

	// Specialties for bus types...
	switch (busType) {
	case qtractorTrack::Audio:
		pUpdateBusCommand->setChannels(
			m_ui.AudioChannelsSpinBox->value());
		pUpdateBusCommand->setAutoConnect(
			m_ui.AudioAutoConnectCheckBox->isChecked());
		break;
	case qtractorTrack::Midi:
	case qtractorTrack::None:
	default:
		break;
	}

	// Execute and refresh form...
	return pSession->execute(pUpdateBusCommand);
}


// Create a new bus from current view.
void qtractorBusForm::createBus (void)
{
	if (m_pBus == NULL)
		return;

	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession == NULL)
		return;
	
	const QString sBusName = m_ui.BusNameLineEdit->text().simplified();
	if (sBusName.isEmpty())
		return;

	// Reset plugin lists...
	m_ui.InputPluginListView->setPluginList(NULL);
	m_ui.OutputPluginListView->setPluginList(NULL);

	qtractorBus::BusMode busMode = qtractorBus::None;
	switch (m_ui.BusModeComboBox->currentIndex()) {
	case 0:
		busMode = qtractorBus::Input;
		break;
	case 1:
		busMode = qtractorBus::Output;
		break;
	case 2:
		busMode = qtractorBus::Duplex;
		break;
	}

	// Make it as an unduable command...
	qtractorCreateBusCommand *pCreateBusCommand
		= new qtractorCreateBusCommand();

	// Set all creational properties...
	qtractorTrack::TrackType busType = m_pBus->busType();
	pCreateBusCommand->setBusType(busType);
	pCreateBusCommand->setBusName(sBusName);
	pCreateBusCommand->setBusMode(busMode);	
	pCreateBusCommand->setPassthru(
		(busMode & qtractorBus::Duplex) == qtractorBus::Duplex
		&& m_ui.PassthruCheckBox->isChecked());

	// Specialties for bus types...
	switch (busType) {
	case qtractorTrack::Audio:
		pCreateBusCommand->setChannels(
			m_ui.AudioChannelsSpinBox->value());
		pCreateBusCommand->setAutoConnect(
			m_ui.AudioAutoConnectCheckBox->isChecked());
		break;
	case qtractorTrack::Midi:
	case qtractorTrack::None:
	default:
		break;
	}

	// Execute and refresh form...
	if (pSession->execute(pCreateBusCommand)) {
		m_iDirtyTotal++;
		refreshBuses();
	}

	// Reselect current bus...
	setBus(m_pBus);
}


// Update current bus in view.
void qtractorBusForm::updateBus (void)
{
	// That's it...
	if (updateBusEx(m_pBus)) {
		m_iDirtyTotal++;
		refreshBuses();
	}

	// Reselect current bus...
	setBus(m_pBus);
}


// Delete current bus in view.
void qtractorBusForm::deleteBus (void)
{
	if (m_pBus == NULL)
		return;

	qtractorSession *pSession = qtractorSession::getInstance();
	if (pSession == NULL)
		return;

	// Prompt user if he/she's sure about this...
	qtractorOptions *pOptions = qtractorOptions::getInstance();
	if (pOptions && pOptions->bConfirmRemove) {
		// Get some textual type...
		QString sBusType;
		switch (m_pBus->busType()) {
		case qtractorTrack::Audio:
			sBusType = tr("Audio");
			break;
		case qtractorTrack::Midi:
			sBusType = tr("MIDI");
			break;
		default:
			break;
		}
		// Show the warning...
		if (QMessageBox::warning(this,
			tr("Warning") + " - " QTRACTOR_TITLE,
			tr("About to remove bus:\n\n"
			"\"%1\" (%2)\n\n"
			"Are you sure?")
			.arg(m_pBus->busName())
			.arg(sBusType),
			QMessageBox::Ok | QMessageBox::Cancel) == QMessageBox::Cancel)
			return;
	}

	// Reset plugin lists...
	m_ui.InputPluginListView->setPluginList(NULL);
	m_ui.OutputPluginListView->setPluginList(NULL);

	// Make it as an unduable command...
	qtractorDeleteBusCommand *pDeleteBusCommand
		= new qtractorDeleteBusCommand(m_pBus);

	// Invalidade current bus...
	m_pBus = NULL;

	// Execute and refresh form...
	if (pSession->execute(pDeleteBusCommand)) {
		m_iDirtyTotal++;
		refreshBuses();
	}

	// Done.
	stabilizeForm();
}


// Make changes due.
void qtractorBusForm::changed (void)
{
	if (m_iDirtySetup > 0)
		return;

	m_iDirtyCount++;
	stabilizeForm();
}


// Reject settings (Close button slot).
void qtractorBusForm::reject (void)
{
	bool bReject = true;

	// Check if there's any pending changes...
	if (m_iDirtyCount > 0) {
		switch (QMessageBox::warning(this,
			tr("Warning") + " - " QTRACTOR_TITLE,
			tr("Some settings have been changed.\n\n"
			"Do you want to discard the changes?"),
			QMessageBox::Discard | QMessageBox::Cancel)) {
		case QMessageBox::Discard:
			break;
		default:    // Cancel.
			bReject = false;
			break;
		}
	}

	if (bReject)
		QDialog::reject();
}


// Stabilize current form state.
void qtractorBusForm::stabilizeForm (void)
{
	if (m_pBus) {
		m_ui.CommonBusGroup->setEnabled(true);
		m_ui.AudioBusGroup->setEnabled(m_pBus->busType() == qtractorTrack::Audio);
	} else {
		m_ui.CommonBusGroup->setEnabled(false);
		m_ui.AudioBusGroup->setEnabled(false);
	}

	m_ui.PassthruCheckBox->setEnabled(
		m_pBus && m_ui.BusModeComboBox->currentIndex() == 2);

	m_ui.RefreshPushButton->setEnabled(m_iDirtyCount > 0);
	m_ui.CreatePushButton->setEnabled(canCreateBus());
	m_ui.UpdatePushButton->setEnabled(canUpdateBus());
	m_ui.DeletePushButton->setEnabled(canDeleteBus());

	// Stabilize current plugin lists state.
	bool bEnabled;
	int iItem, iItemCount;
	qtractorPlugin *pPlugin = NULL;
	qtractorPluginListItem *pItem = NULL;

	// Input plugin list...
	bEnabled = (m_ui.InputPluginListView->pluginList() != NULL);
	m_ui.BusTabWidget->setTabEnabled(1, bEnabled);
	if (bEnabled) {
		iItemCount = m_ui.InputPluginListView->count();
		iItem = -1;
		pPlugin = NULL;
		pItem = static_cast<qtractorPluginListItem *> (
			m_ui.InputPluginListView->currentItem());
		if (pItem) {
			iItem = m_ui.InputPluginListView->row(pItem);
			pPlugin = pItem->plugin();
		}
	//	m_ui.AddInputPluginToolButton->setEnabled(true);
		m_ui.RemoveInputPluginToolButton->setEnabled(pPlugin != NULL);
		m_ui.MoveUpInputPluginToolButton->setEnabled(pItem && iItem > 0);
		m_ui.MoveDownInputPluginToolButton->setEnabled(
			pItem && iItem < iItemCount - 1);
	}

	// Output plugin list...
	bEnabled = (m_ui.OutputPluginListView->pluginList() != NULL);
	m_ui.BusTabWidget->setTabEnabled(2, bEnabled);
	if (bEnabled) {
		iItemCount = m_ui.OutputPluginListView->count();
		iItem = -1;
		pPlugin = NULL;
		pItem = static_cast<qtractorPluginListItem *> (
			m_ui.OutputPluginListView->currentItem());
		if (pItem) {
			iItem = m_ui.OutputPluginListView->row(pItem);
			pPlugin = pItem->plugin();
		}
	//	m_ui.AddOutputPluginToolButton->setEnabled(true);
		m_ui.RemoveOutputPluginToolButton->setEnabled(pPlugin != NULL);
		m_ui.MoveUpOutputPluginToolButton->setEnabled(pItem && iItem > 0);
		m_ui.MoveDownOutputPluginToolButton->setEnabled(
			pItem && iItem < iItemCount - 1);
	}
}


// Bus list view context menu handler.
void qtractorBusForm::contextMenu ( const QPoint& /*pos*/ )
{
	// Build the device context menu...
	QMenu menu(this);
	QAction *pAction;
	
	pAction = menu.addAction(
		QIcon(":/icons/formCreate.png"),
		tr("&Create"), this, SLOT(createBus()));
	pAction->setEnabled(canCreateBus());

	pAction = menu.addAction(
		QIcon(":/icons/formAccept.png"),
		tr("&Update"), this, SLOT(updateBus()));
	pAction->setEnabled(canUpdateBus());

	pAction = menu.addAction(
		QIcon(":/icons/formRemove.png"),
		tr("&Delete"), this, SLOT(deleteBus()));
	pAction->setEnabled(canDeleteBus());

	menu.addSeparator();

	pAction = menu.addAction(
		QIcon(":/icons/formRefresh.png"),
		tr("&Refresh"), this, SLOT(refreshBuses()));
	pAction->setEnabled(m_iDirtyCount > 0);

//	menu.exec(m_ui.BusListView->mapToGlobal(pos));
	menu.exec(QCursor::pos());
}


// Input plugin list slots.
void qtractorBusForm::addInputPlugin (void)
{
	m_ui.InputPluginListView->addPlugin();
	stabilizeForm();
}

void qtractorBusForm::removeInputPlugin (void)
{
	m_ui.InputPluginListView->removePlugin();
	stabilizeForm();
}

void qtractorBusForm::moveUpInputPlugin (void)
{
	m_ui.InputPluginListView->moveUpPlugin();
	stabilizeForm();
}

void qtractorBusForm::moveDownInputPlugin (void)
{
	m_ui.InputPluginListView->moveDownPlugin();
	stabilizeForm();
}


// Output plugin list slots.
void qtractorBusForm::addOutputPlugin (void)
{
	m_ui.OutputPluginListView->addPlugin();
	stabilizeForm();
}

void qtractorBusForm::removeOutputPlugin (void)
{
	m_ui.OutputPluginListView->removePlugin();
	stabilizeForm();
}

void qtractorBusForm::moveUpOutputPlugin (void)
{
	m_ui.OutputPluginListView->moveUpPlugin();
	stabilizeForm();
}

void qtractorBusForm::moveDownOutputPlugin (void)
{
	m_ui.OutputPluginListView->moveDownPlugin();
	stabilizeForm();
}


// end of qtractorBusForm.cpp
