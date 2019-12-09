/* === This file is part of Calamares - <https://github.com/calamares> ===
 *
 *   Copyright 2015, Anke Boersma <demm@kaosx.us>
 *   Copyright 2015, Alexandre Arnt <qtgzmanager@gmail.com>
 *   Copyright 2015, Teo Mrnjavac <teo@kde.org>
 *   Copyright 2018, Adriaan de Groot <groot@kde.org>
 *
 *   Calamares is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   Calamares is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Calamares. If not, see <http://www.gnu.org/licenses/>.
 */

#include "LicensePage.h"

#include "LicenseWidget.h"
#include "ui_LicensePage.h"

#include "GlobalStorage.h"
#include "JobQueue.h"
#include "ViewManager.h"

#include "utils/CalamaresUtilsGui.h"
#include "utils/Logger.h"
#include "utils/NamedEnum.h"
#include "utils/Retranslator.h"
#include "utils/Variant.h"

#include <QApplication>
#include <QBoxLayout>
#include <QComboBox>
#include <QDesktopServices>
#include <QFocusEvent>
#include <QLabel>
#include <QMessageBox>

#include <algorithm>

static const char mustAccept[] = "#acceptFrame { border: 1px solid red;"
                                 "background-color: #fff6f6;"
                                 "border-radius: 4px;"
                                 "padding: 2px; }";
static const char okAccept[] = "#acceptFrame { padding: 3px }";

const NamedEnumTable< LicenseEntry::Type >&
LicenseEntry::typeNames()
{
    static const NamedEnumTable< LicenseEntry::Type > names {
        { QStringLiteral( "software" ), LicenseEntry::Type::Software },
        { QStringLiteral( "driver" ), LicenseEntry::Type::Driver },
        { QStringLiteral( "gpudriver" ), LicenseEntry::Type::GpuDriver },
        { QStringLiteral( "browserplugin" ), LicenseEntry::Type::BrowserPlugin },
        { QStringLiteral( "codec" ), LicenseEntry::Type::Codec },
        { QStringLiteral( "package" ), LicenseEntry::Type::Package }
    };

    return names;
}

LicenseEntry::LicenseEntry( const QVariantMap& conf )
{
    if ( !conf.contains( "id" ) || !conf.contains( "name" ) || !conf.contains( "url" ) )
    {
        return;
    }

    m_id = conf[ "id" ].toString();
    m_prettyName = conf[ "name" ].toString();
    m_prettyVendor = conf.value( "vendor" ).toString();
    m_url = QUrl( conf[ "url" ].toString() );

    m_required = CalamaresUtils::getBool( conf, "required", false );
    m_expand = CalamaresUtils::getBool( conf, "expand", false );

    bool ok = false;
    QString typeString = conf.value( "type", "software" ).toString();
    m_type = typeNames().find( typeString, ok );
    if ( !ok )
    {
        cWarning() << "License entry" << m_id << "has unknown type" << typeString << "(using 'software')";
    }
}

bool
LicenseEntry::isLocal() const
{
    return m_url.isLocalFile();
}


LicensePage::LicensePage( QWidget* parent )
    : QWidget( parent )
    , m_isNextEnabled( false )
    , m_allLicensesOptional( false )
    , ui( new Ui::LicensePage )
{
    ui->setupUi( this );

    // ui->verticalLayout->insertSpacing( 1, CalamaresUtils::defaultFontHeight() );
    CalamaresUtils::unmarginLayout( ui->verticalLayout );

    ui->mainText->setWordWrap( true );
    ui->mainText->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Minimum );

    ui->acceptFrame->setFrameStyle( QFrame::NoFrame | QFrame::Plain );
    ui->acceptFrame->setStyleSheet( mustAccept );
    ui->acceptFrame->layout()->setMargin( CalamaresUtils::defaultFontHeight() / 2 );

    updateGlobalStorage( false );  // Have not agreed yet

    connect( ui->acceptCheckBox, &QCheckBox::toggled, this, &LicensePage::checkAcceptance );

    CALAMARES_RETRANSLATE_SLOT( &LicensePage::retranslate )
}

void
LicensePage::setEntries( const QList< LicenseEntry >& entriesList )
{
    CalamaresUtils::clearLayout( ui->licenseEntriesLayout );

    m_allLicensesOptional = true;

    m_entries.clear();
    m_entries.reserve( entriesList.count() );
    for ( const LicenseEntry& entry : entriesList )
    {
        LicenseWidget* w = new LicenseWidget( entry );
        ui->licenseEntriesLayout->addWidget( w );
        m_entries.append( w );
        m_allLicensesOptional &= !entry.isRequired();
    }

    ui->acceptCheckBox->setChecked( false );
    checkAcceptance( false );
}

void
LicensePage::retranslate()
{
    ui->acceptCheckBox->setText( tr( "I accept the terms and conditions above." ) );

    QString review = tr( "Please review the End User License Agreements (EULAs)." );
    const auto br = QStringLiteral( "<br/>" );

    if ( !m_allLicensesOptional )
    {
        ui->mainText->setText( tr( "This setup procedure will install proprietary "
                                   "software that is subject to licensing terms." )
                               + br + review );
        QString mustAcceptText( tr( "If you do not agree with the terms, the setup procedure cannot continue." ) );
        ui->acceptCheckBox->setToolTip( mustAcceptText );
    }
    else
    {
        ui->mainText->setText( tr( "This setup procedure can install proprietary "
                                   "software that is subject to licensing terms "
                                   "in order to provide additional features and enhance the user "
                                   "experience." )
                               + br + review );
        QString okAcceptText( tr( "If you do not agree with the terms, proprietary software will not "
                                  "be installed, and open source alternatives will be used instead." ) );
        ui->acceptCheckBox->setToolTip( okAcceptText );
    }
    ui->retranslateUi( this );

    for ( const auto& w : m_entries )
    {
        w->retranslateUi();
    }
}


bool
LicensePage::isNextEnabled() const
{
    return m_isNextEnabled;
}

void
LicensePage::updateGlobalStorage( bool v )
{
    Calamares::JobQueue::instance()->globalStorage()->insert( "licenseAgree", v );
}

void
LicensePage::checkAcceptance( bool checked )
{
    updateGlobalStorage( checked );

    m_isNextEnabled = checked || m_allLicensesOptional;
    if ( !m_isNextEnabled )
    {
        ui->acceptFrame->setStyleSheet( mustAccept );
    }
    else
    {
        ui->acceptFrame->setStyleSheet( okAccept );
    }
    emit nextStatusChanged( m_isNextEnabled );
}
