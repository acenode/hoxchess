/***************************************************************************
 *  Copyright 2007, 2008 Huy Phan  <huyphan@playxiangqi.com>               *
 *                                                                         * 
 *  This file is part of HOXChess.                                         *
 *                                                                         *
 *  HOXChess is free software: you can redistribute it and/or modify       *
 *  it under the terms of the GNU General Public License as published by   *
 *  the Free Software Foundation, either version 3 of the License, or      *
 *  (at your option) any later version.                                    *
 *                                                                         *
 *  HOXChess is distributed in the hope that it will be useful,            *
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
 *  GNU General Public License for more details.                           *
 *                                                                         *
 *  You should have received a copy of the GNU General Public License      *
 *  along with HOXChess.  If not, see <http://www.gnu.org/licenses/>.      *
 ***************************************************************************/

/////////////////////////////////////////////////////////////////////////////
// Name:            hoxSite.cpp
// Created:         11/24/2007
//
// Description:     The Site.
/////////////////////////////////////////////////////////////////////////////

#include "hoxSite.h"
#include "MyApp.h"
#include "hoxServer.h"
#include "hoxUtil.h"
#include "hoxSocketConnection.h"
#include "hoxHttpConnection.h"
#include "hoxChesscapeConnection.h"
#include "hoxChesscapePlayer.h"
#include "MyFrame.h"
#include "MyChild.h"
#include "hoxNetworkAPI.h"
#include "hoxTablesDialog.h"


// --------------------------------------------------------------------------
// hoxSite
// --------------------------------------------------------------------------


hoxSite::hoxSite( hoxSiteType             type,
                  const hoxServerAddress& address )
        : m_type( type )
        , m_address( address)
        , m_dlgProgress( NULL )
		, m_siteClosing( false )
{
    m_playerMgr.SetSite( this );
    m_tableMgr.SetSite( this );
}

hoxSite::~hoxSite()
{
}

void 
hoxSite::DeletePlayer( hoxPlayer* player )
{ 
	m_playerMgr.DeletePlayer( player ); 
}

hoxResult 
hoxSite::CloseTable(hoxTable* table)
{
    m_tableMgr.RemoveTable( table );
    return hoxRC_OK;
}

void 
hoxSite::Handle_DisconnectFromPlayer( hoxPlayer* player )
{
    const char* FNAME = "hoxSite::Handle_DisconnectFromPlayer";
    
	wxLogDebug("%s: ENTER. Do nothing. END.", FNAME);
}

void 
hoxSite::Handle_ShutdownReadyFromPlayer( const wxString& playerId )
{
    const char* FNAME = "hoxSite::Handle_ShutdownReadyFromPlayer";
    
	wxLogDebug("%s: ENTER. Do nothing. END.", FNAME);
}

void
hoxSite::ShowProgressDialog( bool bShow /* = true */ )
{
    const char* FNAME = "hoxSite::ShowProgressDialog";

    if ( bShow )
    {
        if ( m_dlgProgress != NULL ) 
        {
            m_dlgProgress->Destroy();  // NOTE: ... see wxWidgets' documentation.
            m_dlgProgress = NULL;
        }

        m_dlgProgress = new wxProgressDialog(
            "Progress dialog",
            "Wait until connnection is established or press [Cancel]",
            100,
            wxGetApp().GetFrame(),  // parent
            wxPD_AUTO_HIDE | wxPD_CAN_ABORT
            );
        m_dlgProgress->SetSize( wxSize(500, 150) );
        m_dlgProgress->Pulse();
    }
    else /* Hide */
    {
        if ( m_dlgProgress != NULL )
        {
            bool wasCanceled = !m_dlgProgress->Pulse();
            m_dlgProgress->Update(100);  // make sure to close the dialog.
            if ( wasCanceled )
            {
                wxLogDebug("%s: Connection has been canceled.", FNAME);
                return;
            }
        }
    }
}

// --------------------------------------------------------------------------
// hoxLocalSite
// --------------------------------------------------------------------------


hoxLocalSite::hoxLocalSite(const hoxServerAddress& address)
        : hoxSite( hoxSITE_TYPE_LOCAL, address )
        , m_server( NULL )
        , m_isOpened( false )
		, m_nNextTableId( 0 )
{
}

hoxLocalSite::~hoxLocalSite()
{
}

const wxString 
hoxLocalSite::GetName() const
{
    wxString name;

    name.Printf("Local Site (%d)", m_address.port);
    return name;
}

hoxResult 
hoxLocalSite::OpenServer()
{
    const char* FNAME = "hoxLocalSite::OpenServer";
    wxLogDebug("%s: ENTER.", FNAME);

    if ( m_server != NULL )
    {
        wxLogDebug("%s: The server have been created. END.", FNAME);
        return hoxRC_OK;
    }

    m_server = new hoxServer( this );

	if ( hoxRC_OK !=  m_server->StartServer( m_address.port ) )
	{
        wxLogError("%s: Failed to start socker-server thread.", FNAME);
        return hoxRC_ERR;
	}

    m_isOpened = true;

    return hoxRC_OK;
}

hoxResult
hoxLocalSite::Close()
{
    const char* FNAME = "hoxLocalSite::Close";

	if ( m_siteClosing )
	{
		wxLogDebug("%s: Site [%s] is already being closed. END.", FNAME, this->GetName().c_str());
		return hoxRC_OK;
	}
	m_siteClosing = true;

	m_playerMgr.OnSiteClosing();

	if ( m_playerMgr.GetNumberOfPlayers() == 0 )	
	{
		_DoCloseSite();
	}

    return hoxRC_OK;
}

hoxResult 
hoxLocalSite::CreateNewTableAsPlayer( wxString&          newTableId, 
                                      hoxPlayer*         player,
									  const hoxTimeInfo& initialTime )
{
    const char* FNAME = "hoxLocalSite::CreateNewTableAsPlayer";
    hoxTable* newTable = NULL;

    wxLogDebug("%s: ENTER.", FNAME);

	/* Generate a new Table-Id */
	newTableId = _GenerateTableId();

    /* Create a new table without a frame. */
    newTable = m_tableMgr.CreateTable( newTableId );
	newTable->SetInitialTime( initialTime );
    newTable->SetRedTime( initialTime );
	newTable->SetBlackTime( initialTime );

    /* Add the specified player to the table. */
    hoxResult result = player->JoinTableAs( newTable, hoxCOLOR_RED );
    wxASSERT( result == hoxRC_OK  );

    /* Update UI. */
    wxGetApp().GetFrame()->UpdateSiteTreeUI();

    return hoxRC_OK;
}

void 
hoxLocalSite::Handle_DisconnectFromPlayer( hoxPlayer* player )
{
    const char* FNAME = "hoxLocalSite::Handle_DisconnectFromPlayer";
    wxLogDebug("%s: ENTER.", FNAME);

	/* Inform the server. */
    wxLogDebug("%s: Posting DISCONNECT request to remove the new client connection.", FNAME);
    hoxRequest* request = new hoxRequest( hoxREQUEST_LOGOUT );
    request->parameters["pid"] = player->GetName();
    m_server->AddRequest( request );

	wxLogDebug("%s: END.", FNAME);
}

void 
hoxLocalSite::Handle_ShutdownReadyFromPlayer( const wxString& playerId )
{
    const char* FNAME = "hoxLocalSite::Handle_ShutdownReadyFromPlayer";
    wxLogDebug("%s: ENTER. (%s)", FNAME, playerId.c_str());

	hoxPlayer* player = this->FindPlayer( playerId );
	if ( player == NULL )
	{
		wxLogDebug("%s: *** WARN *** Player [%s] not found. END.", FNAME, playerId.c_str());
		return;
	}

	this->DeletePlayer( player );

    /* Perform the actual closing. */
	if ( m_playerMgr.GetNumberOfPlayers() == 0 )	
	{
		wxLogDebug("%s: The number of players = 0. Closing site...", FNAME);
		_DoCloseSite();
	}
}

void 
hoxLocalSite::_DoCloseSite()
{
	const char* FNAME = "hoxLocalSite::_DoCloseSite";

	wxLogDebug("%s: ENTER.", FNAME);

	if ( m_server != NULL )
	{
		m_server->CloseServer();
		delete m_server;
		m_server = NULL;
	}

	m_isOpened = false;

	wxCommandEvent event( hoxEVT_APP_SITE_CLOSE_READY );
	event.SetEventObject( this );
	wxPostEvent( &(wxGetApp()), event );
}

const wxString 
hoxLocalSite::_GenerateTableId()
{
	++m_nNextTableId;
	return wxString::Format("%d", m_nNextTableId);
}


// --------------------------------------------------------------------------
// hoxRemoteSite
// --------------------------------------------------------------------------

hoxRemoteSite::hoxRemoteSite(const hoxServerAddress& address,
                             hoxSiteType             type /*= hoxSITE_TYPE_REMOTE*/)
        : hoxSite( type, address )
        , m_player( NULL )
{
    const char* FNAME = "hoxRemoteSite::hoxRemoteSite";
    wxLogDebug("%s: ENTER.", FNAME);
}

hoxRemoteSite::~hoxRemoteSite()
{
    const char* FNAME = "hoxRemoteSite::~hoxRemoteSite";
    wxLogDebug("%s: ENTER.", FNAME);
}

hoxLocalPlayer* 
hoxRemoteSite::CreateLocalPlayer( const wxString& playerName )
{
	wxCHECK_MSG(m_player == NULL, NULL, "The player has already been set.");

	m_player = m_playerMgr.CreateMyPlayer( playerName );
	return m_player;
}

void 
hoxRemoteSite::OnResponse_LOGIN( const hoxResponse_APtr& response )
{
    const char* FNAME = "hoxRemoteSite::OnResponse_LOGIN";

    this->ShowProgressDialog( false );

    /* If error, then close the Site. */

    if ( response->code != hoxRC_OK )
    {
        wxLogDebug("%s: The response's code for [%s] is ERROR [%d].", 
            FNAME, 
            hoxUtil::RequestTypeToString(response->type).c_str(), 
            response->code);
        this->Handle_ShutdownReadyFromPlayer( m_player ? m_player->GetName() : "" );
        return;
    }
}

void 
hoxRemoteSite::OnResponse_LOGOUT( const hoxResponse_APtr& response )
{
    const char* FNAME = "hoxRemoteSite::OnResponse_LOGOUT";

	wxLogDebug("%s: Received DISCONNECT's response [%d: %s].", 
		FNAME, response->code, response->content.c_str());

    m_player->OnClosing_FromSite();
}

hoxResult
hoxRemoteSite::OnPlayerJoined( const wxString&  tableId,
                               const wxString&  playerId,
                               const int        playerScore,
				 			   const hoxColor   requestColor)
{
    const char* FNAME = "hoxRemoteSite::OnPlayerJoined";
	hoxResult   result;
    hoxTable*   table = NULL;
    hoxPlayer*  player = NULL;

	/* Lookup the Table.
     * Make sure that it must be already created.
     */
	table = this->FindTable( tableId );
	if ( table == NULL )
	{
        wxLogDebug("%s: *** WARN *** The table [%s] does NOT exist.", FNAME, tableId.c_str());
		return hoxRC_ERR;
	}

	/* Lookup the Player.
     * If not found, then create a new "dummy" player.
     */
    player = this->FindPlayer( playerId );
	if ( player == NULL )
	{
        player = this->CreateDummyPlayer( playerId, playerScore );
	}

    /* Attempt to join the table with the requested color.
     */
    result = player->JoinTableAs( table, requestColor );
    if ( result != hoxRC_OK )
    {
        wxLogDebug("%s: *** ERROR *** Failed to ask table to join [%s] as color [%d].", 
            FNAME, playerId.c_str(), requestColor);
        // NOTE: If a new player was created, just leave it as is.
        return hoxRC_ERR;
    }

	/* Toggle board if the new joined Player is I (this site's player) 
     * and I play BLACK.
     */

    if (    player == this->m_player 
         && requestColor == hoxCOLOR_BLACK )
	{
		table->ToggleViewSide();
	}

	return hoxRC_OK;
}

hoxResult 
hoxRemoteSite::JoinNewTable(const hoxNetworkTableInfo& tableInfo)
{
	const char* FNAME = "hoxRemoteSite::JoinNewTable";
    hoxResult      result;
    hoxTable*      table   = NULL;
    const wxString tableId = tableInfo.id;
    const wxString redId   = tableInfo.redId;
    const wxString blackId = tableInfo.blackId;
    hoxPlayer*     player  = NULL;  // Just a player holder.

	/* Create a table if necessary. */

    table = this->FindTable( tableId );
	if ( table == NULL )
	{
        wxLogDebug("%s: Create a new Table [%s].", FNAME, tableId.c_str());
        table = this->CreateNewTableWithGUI( tableInfo );
	}

	/* Determine which color (or role) my player will have. */
	
	hoxColor myColor = hoxCOLOR_UNKNOWN;

	if      ( redId == m_player->GetName() )   myColor = hoxCOLOR_RED;
	else if ( blackId == m_player->GetName() ) myColor = hoxCOLOR_BLACK;
    else 	                                   myColor = hoxCOLOR_NONE;

	/****************************
	 * Assign players to table.
     ****************************/

    result = m_player->JoinTableAs( table, myColor );
    wxCHECK( result == hoxRC_OK, hoxRC_ERR  );

	/* Create additional "dummy" player(s) if required.
     */

    if ( !redId.empty() && table->GetRedPlayer() == NULL )
    {
	    if ( NULL == (player = this->FindPlayer( redId )) )
	    {
            player = this->CreateDummyPlayer( redId, ::atoi(tableInfo.redScore) );
	    }
        result = player->JoinTableAs( table, hoxCOLOR_RED );
        wxCHECK( result == hoxRC_OK, hoxRC_ERR  );
    }
    if ( !blackId.empty() && table->GetBlackPlayer() == NULL )
    {
	    if ( NULL == (player = this->FindPlayer( blackId )) )
	    {
            player = this->CreateDummyPlayer( blackId, ::atoi(tableInfo.blackScore) );
	    }
        result = player->JoinTableAs( table, hoxCOLOR_BLACK );
        wxCHECK( result == hoxRC_OK, hoxRC_ERR  );
    }

	/* Toggle board if I play BLACK.
     */

    if ( myColor == hoxCOLOR_BLACK )
	{
		table->ToggleViewSide();
	}

	wxGetApp().GetFrame()->UpdateSiteTreeUI();

	return hoxRC_OK;
}

hoxResult
hoxRemoteSite::DisplayListOfTables( const hoxNetworkTableInfoList& tableList )
{
    const char* FNAME = "hoxRemoteSite::DisplayListOfTables";
    hoxResult   result;

    wxLogDebug("%s: ENTER.", FNAME);

    /* Show tables. */
    MyFrame* frame = wxGetApp().GetFrame();
	unsigned int actionFlags = this->GetCurrentActionFlags();
    hoxTablesDialog tablesDlg( frame, wxID_ANY, "Tables", tableList, actionFlags );
    tablesDlg.ShowModal();
    hoxTablesDialog::CommandId selectedCommand = tablesDlg.GetSelectedCommand();
    wxString selectedId = tablesDlg.GetSelectedId();

    /* Find out which command the use wants to execute... */

    switch( selectedCommand )
    {
        case hoxTablesDialog::COMMAND_ID_JOIN:
        {
            wxLogDebug("%s: Ask the server to allow me to JOIN table = [%s]", FNAME, selectedId.c_str());
            result = m_player->JoinNetworkTable( selectedId );
            if ( result != hoxRC_OK )
            {
                wxLogError("%s: Failed to JOIN a network table [%s].", FNAME, selectedId.c_str());
            }
            break;
        }

        case hoxTablesDialog::COMMAND_ID_NEW:
        {
            wxLogDebug("%s: Ask the server to open a new table.", FNAME);
            result = m_player->OpenNewNetworkTable();
            if ( result != hoxRC_OK )
            {
                wxLogError("%s: Failed to open a NEW network table.", FNAME);
            }
            break;
        }

		case hoxTablesDialog::COMMAND_ID_REFRESH:
        {
            wxLogDebug("%s: Get the latest list of tables...", FNAME);
			result = this->QueryForNetworkTables();
            if ( result != hoxRC_OK )
            {
                wxLogError("%s: Failed to get the list of tables.", FNAME);
            }
            break;
        }

        default:
            wxLogDebug("%s: No command is selected. Fine.", FNAME);
            break;
    }

    return hoxRC_OK;
}

const wxString 
hoxRemoteSite::GetName() const
{
    wxString name;

	name.Printf("%s:%d", m_address.name.c_str(), m_address.port);
	//if ( ! this->IsConnected() )
	//{
	//	name += " (disconnected)";		
	//}

    return name;
}

hoxResult 
hoxRemoteSite::Connect()
{
    const char* FNAME = "hoxRemoteSite::Connect";
    hoxResult result;

    if ( this->IsConnected() )
    {
        wxLogDebug("%s: This site has been connected. END.", FNAME);
        return hoxRC_OK;
    }

    /* Start connecting... */

    this->ShowProgressDialog( true );

    result = m_player->ConnectToNetworkServer();
    if ( result != hoxRC_OK )
    {
        wxLogError("%s: Failed to connect to server.", FNAME);
        return hoxRC_ERR;
    }

    return hoxRC_OK;
}

hoxResult 
hoxRemoteSite::Close()
{
    const char* FNAME = "hoxRemoteSite::Close";
	hoxResult result;
    wxLogDebug("%s: ENTER.", FNAME);

	if ( m_siteClosing )
	{
		wxLogDebug("%s: Site [%s] is already being closed. END.", FNAME, this->GetName().c_str());
		return hoxRC_OK;
	}
	m_siteClosing = true;

	if ( m_player != NULL )
	{
		/* Inform the remote server that the player is logging-out. 
		 */
		result = m_player->DisconnectFromNetworkServer();
		if ( result != hoxRC_OK )
		{
			wxLogError("%s: Failed to connect to server.", FNAME);
			return hoxRC_ERR;
		}
	}

    return hoxRC_OK;
}

hoxResult 
hoxRemoteSite::QueryForNetworkTables()
{
    const char* FNAME = "hoxRemoteSite::QueryForNetworkTables";
    hoxResult result;

    if ( ! this->IsConnected() )
    {
        wxLogDebug("%s: This site has NOT been connected.", FNAME);
        return hoxRC_ERR;
    }

    result = m_player->QueryForNetworkTables();
    if ( result != hoxRC_OK )
    {
        wxLogError("%s: Failed to query for tables.", FNAME);
        return hoxRC_ERR;
    }

    return hoxRC_OK;
}

bool 
hoxRemoteSite::IsConnected() const
{
    return (    m_player != NULL
		     && m_player->GetConnection() != NULL
             && m_player->GetConnection()->IsConnected() );
}

hoxResult 
hoxRemoteSite::CreateNewTable( wxString& newTableId )
{
    const char* FNAME = "hoxRemoteSite::CreateNewTable";
    hoxResult result;

    result = m_player->OpenNewNetworkTable();
    if ( result != hoxRC_OK )
    {
        wxLogError("%s: Failed to open a new Table on the remote server.", FNAME);
        return hoxRC_ERR;
    }

    return hoxRC_OK;
}

hoxResult 
hoxRemoteSite::JoinExistingTable(const hoxNetworkTableInfo& tableInfo)
{
    const char* FNAME = "hoxRemoteSite::JoinExistingTable";
	hoxResult   result;
    hoxTable*   table = NULL;
    wxString    tableId = tableInfo.id;

	/* Make sure that no Table with the specified Id is created yet. */
	table = this->FindTable( tableId );
	if ( table != NULL )
	{
		wxLogWarning("This Site should only handle JOIN (existing) table.");
		return hoxRC_ERR;
	}
	
    /***********************/
    /* Create a new table. */
    /***********************/

    table = this->CreateNewTableWithGUI( tableInfo );

    /***********************/
    /* Setup players       */
    /***********************/

	hoxColor myColor = hoxCOLOR_NONE;
    hoxPlayer*    red_player = NULL;
    hoxPlayer*    black_player = NULL;
	int           score = 0;

	/* Determine which color my player should play (RED/BLACK/OBSERVER)? */

	if ( tableInfo.redId == m_player->GetName() )
	{
		myColor = hoxCOLOR_RED;
		red_player = m_player;
	}
	else if ( tableInfo.blackId == m_player->GetName() )
	{
		myColor = hoxCOLOR_BLACK;
		black_player = m_player;
	}

	/* Create additional Dummy player(s) as required. */

	if ( red_player == NULL && !tableInfo.redId.empty() )
	{
		score = ::atoi( tableInfo.redScore.c_str() ); 
		red_player = m_playerMgr.CreateDummyPlayer( tableInfo.redId, score);
	}
	if ( black_player == NULL && !tableInfo.blackId.empty() )
	{
		score = ::atoi( tableInfo.blackScore.c_str() ); 
		black_player = m_playerMgr.CreateDummyPlayer( tableInfo.blackId, score);
	}

    /* Join the players at the table. */

    if ( red_player != NULL )
    {
        result = red_player->JoinTableAs( table, hoxCOLOR_RED );
        wxASSERT( result == hoxRC_OK  );
    }
    if ( black_player != NULL )
    {
        result = black_player->JoinTableAs( table, hoxCOLOR_BLACK );
        wxASSERT( result == hoxRC_OK  );
    }
	if ( myColor == hoxCOLOR_NONE )
	{
		result = m_player->JoinTableAs( table, myColor );
		wxASSERT( result == hoxRC_OK  );
	}

	/* Toggle board if I play BLACK. */

	if ( myColor == hoxCOLOR_BLACK )
	{
		table->ToggleViewSide();
	}

	wxGetApp().GetFrame()->UpdateSiteTreeUI();

	return hoxRC_OK;
}

void 
hoxRemoteSite::DeletePlayer( hoxPlayer* player )
{
	if ( m_player == player )
	{
		m_player = NULL;
	}

	this->hoxSite::DeletePlayer( player );
}

void 
hoxRemoteSite::Handle_ShutdownReadyFromPlayer( const wxString& playerId )
{
    const char* FNAME = "hoxRemoteSite::Handle_ShutdownReadyFromPlayer";
    wxLogDebug("%s: ENTER.", FNAME);

	if ( m_player == NULL )
	{
		wxLogDebug("%s: Player is NULL. Shutdown must have already been processed.", FNAME);
		return;
	}

	/* Must set the local player to NULL immediately to handle "re-entrance"
	 * because the DISCONNECT call below can go to sleep...
	 */
	hoxLocalPlayer* localPlayer = m_player;
	m_player = NULL;

	bool bSuccess = localPlayer->ResetConnection();
	if ( ! bSuccess )
	{
		wxLogError("%s: Failed to reset the Connection to NULL.", FNAME);
		return;
	}

    /* Inform the App. */
    wxCommandEvent event( hoxEVT_APP_SITE_CLOSE_READY );
    event.SetEventObject( this );
    wxPostEvent( &(wxGetApp()), event );
}

unsigned int 
hoxRemoteSite::GetCurrentActionFlags() const
{
	unsigned int flags = 0;

    if ( ! this->IsConnected() )
	{
		flags |= hoxSITE_ACTION_CONNECT;
	}
	else
    {
		flags |= hoxSITE_ACTION_DISCONNECT;
		flags |= hoxSITE_ACTION_LIST;
		flags |= hoxSITE_ACTION_NEW;
		flags |= hoxSITE_ACTION_JOIN;
    }

	return flags;
}

hoxTable* 
hoxRemoteSite::CreateNewTableWithGUI(const hoxNetworkTableInfo& tableInfo)
{
    hoxTable* table = NULL;
    wxString  tableId = tableInfo.id;

    /* Create a GUI Frame for the new Table. */
    MyChild* childFrame = wxGetApp().GetFrame()->CreateFrameForTable( tableId );

    /* Create a new table with the newly created Frame. */
    table = m_tableMgr.CreateTable( tableId );
	table->SetInitialTime( tableInfo.initialTime );
    table->SetBlackTime( tableInfo.blackTime );
    table->SetRedTime( tableInfo.redTime );
	table->ViewBoard( childFrame );
    childFrame->SetTable( table );
    childFrame->Show( true );

    return table;
}

// --------------------------------------------------------------------------
// hoxHTTPSite
// --------------------------------------------------------------------------

hoxHTTPSite::hoxHTTPSite( const hoxServerAddress& address )
        : hoxRemoteSite( address, hoxSITE_TYPE_HTTP )
{
    const char* FNAME = "hoxHTTPSite::hoxHTTPSite";
    wxLogDebug("%s: ENTER.", FNAME);
}

hoxHTTPSite::~hoxHTTPSite()
{
    const char* FNAME = "hoxHTTPSite::~hoxHTTPSite";
    wxLogDebug("%s: ENTER.", FNAME);
}

hoxLocalPlayer* 
hoxHTTPSite::CreateLocalPlayer( const wxString& playerName )
{
	wxCHECK_MSG(m_player == NULL, NULL, "The player has already been set.");

	m_player = m_playerMgr.CreateHTTPPlayer( playerName );
	return m_player;
}

// --------------------------------------------------------------------------
// hoxChesscapeSite
// --------------------------------------------------------------------------

hoxChesscapeSite::hoxChesscapeSite( const hoxServerAddress& address )
        : hoxRemoteSite( address, hoxSITE_TYPE_CHESSCAPE )
{
    const char* FNAME = "hoxChesscapeSite::hoxChesscapeSite";
    wxLogDebug("%s: ENTER.", FNAME);
}

hoxChesscapeSite::~hoxChesscapeSite()
{
    const char* FNAME = "hoxChesscapeSite::~hoxChesscapeSite";
    wxLogDebug("%s: ENTER.", FNAME);
}

hoxLocalPlayer* 
hoxChesscapeSite::CreateLocalPlayer( const wxString& playerName )
{
	wxCHECK_MSG(m_player == NULL, NULL, "The player has already been set.");

	m_player = m_playerMgr.CreateChesscapePlayer( playerName );
	return m_player;
}

void 
hoxChesscapeSite::OnResponse_LOGIN( const hoxResponse_APtr& response )
{
    const char* FNAME = "hoxChesscapeSite::OnResponse_LOGIN";
    wxLogDebug("%s: ENTER.", FNAME);

    this->ShowProgressDialog( false );

	/* Do nothing. */
    if ( response->code != hoxRC_OK )
    {
		wxLogWarning("Login failed with response = [%s].", response->content.c_str());
        return;
    }
}

unsigned int 
hoxChesscapeSite::GetCurrentActionFlags() const
{
	unsigned int flags = 0;

	/* Get flags from the parent-class. */
	flags = this->hoxRemoteSite::GetCurrentActionFlags();

    if ( this->IsConnected() )
    {
		/* Chesscape can only support 1-table-at-a-time.
		 * If there is alread a table, then disable NEW and JOIN actions.
		 */
		if ( ! this->GetTables().empty() )
		{
			flags &= ~hoxSITE_ACTION_NEW;
			flags &= ~hoxSITE_ACTION_JOIN;
		}
    }

	return flags;
}

///////////////////////////////////////////////////////////////////////////////

// --------------------------------------------------------------------------
// hoxSiteManager
// --------------------------------------------------------------------------

/* Define (initialize) the single instance */
hoxSiteManager* 
hoxSiteManager::m_instance = NULL;

/* static */
hoxSiteManager* 
hoxSiteManager::GetInstance()
{
	if ( m_instance == NULL )
		m_instance = new hoxSiteManager();

	return m_instance;
}

/* private */
hoxSiteManager::hoxSiteManager()
	: m_localSite( NULL )
{

}

hoxSiteManager::~hoxSiteManager()
{

}

hoxSite* 
hoxSiteManager::CreateSite( hoxSiteType             siteType,
						    const hoxServerAddress& address,
				            const wxString&         userName,
						    const wxString&         password )
{
	const char* FNAME = "hoxSiteManager::CreateSite";
	hoxSite* site = NULL;

	switch ( siteType )
	{
	case hoxSITE_TYPE_LOCAL:
	{
        hoxLocalSite* localSite = new hoxLocalSite( address );
		m_localSite = localSite;  // *** Save this local site.
		site = localSite;
		break;
	}
	case hoxSITE_TYPE_REMOTE:
	{
		hoxRemoteSite* remoteSite = new hoxRemoteSite( address );
		hoxLocalPlayer* localPlayer = remoteSite->CreateLocalPlayer( userName );
		localPlayer->SetPassword( password );
        hoxConnection* connection = new hoxSocketConnection( address,
                                                             localPlayer );
		localPlayer->SetConnection( connection );
		site = remoteSite;
		break;
	}
	case hoxSITE_TYPE_HTTP:
	{
		hoxRemoteSite* remoteSite = new hoxHTTPSite( address );
		hoxLocalPlayer* localPlayer = remoteSite->CreateLocalPlayer( userName );
		localPlayer->SetPassword( password );
        hoxConnection* connection = new hoxHttpConnection( address,
                                                           localPlayer );
		localPlayer->SetConnection( connection );
		site = remoteSite;
		break;
	}
	case hoxSITE_TYPE_CHESSCAPE:
	{
		hoxRemoteSite* remoteSite = new hoxChesscapeSite( address );
		hoxLocalPlayer* localPlayer = remoteSite->CreateLocalPlayer( userName );
		localPlayer->SetPassword( password );
        hoxConnection* connection = new hoxChesscapeConnection( address,
                                                                localPlayer );
		localPlayer->SetConnection( connection );
		site = remoteSite;
		break;
	}
	default:
		break;
	}

    m_sites.push_back( site );
	return site;
}

hoxRemoteSite*
hoxSiteManager::FindRemoteSite( const hoxServerAddress& address ) const
{
    /* Search for existing REMOTE site. */
    for ( hoxSiteList::const_iterator it = m_sites.begin();
                                      it != m_sites.end(); ++it )
    {
        if (   (*it)->GetType() != hoxSITE_TYPE_LOCAL
            && (*it)->GetAddress() == address )
        {
            return (hoxRemoteSite*) (*it);
        }
    }

	return NULL;
}

void
hoxSiteManager::DeleteSite( hoxSite* site )
{
    const char* FNAME = "hoxSiteManager::DeleteSite";

    wxCHECK_RET( site != NULL, "The Site must not be NULL." );
    
    wxLogDebug("%s: Deleting site [%s]...", FNAME, site->GetName().c_str());

    delete site;
    m_sites.remove( site );

    if ( site == m_localSite )
        m_localSite = NULL;
}

void 
hoxSiteManager::Close()
{
    const char* FNAME = "hoxSiteManager::Close";
    wxLogDebug("%s: ENTER.", FNAME);

    for ( hoxSiteList::iterator it = m_sites.begin();
                                it != m_sites.end(); ++it )
    {
		(*it)->Close();
    }
}

/************************* END OF FILE ***************************************/
