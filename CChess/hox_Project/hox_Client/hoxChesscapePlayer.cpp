/***************************************************************************
 *  Copyright 2007 Huy Phan  <huyphan@playxiangqi.com>                     *
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
// Name:            hoxChesscapePlayer.cpp
// Created:         12/12/2007
//
// Description:     The Chesscape LOCAL Player.
/////////////////////////////////////////////////////////////////////////////

#include "hoxChesscapePlayer.h"
#include "hoxNetworkAPI.h"
#include "MyApp.h"      // wxGetApp()
#include "MyFrame.h"
#include "hoxUtility.h"
#include <wx/tokenzr.h>

IMPLEMENT_DYNAMIC_CLASS(hoxChesscapePlayer, hoxLocalPlayer)

BEGIN_EVENT_TABLE(hoxChesscapePlayer, hoxLocalPlayer)
    EVT_SOCKET(CLIENT_SOCKET_ID,  hoxChesscapePlayer::OnIncomingNetworkData)
    EVT_COMMAND(hoxREQUEST_TYPE_PLAYER_DATA, hoxEVT_CONNECTION_RESPONSE, hoxChesscapePlayer::OnConnectionResponse_PlayerData)
    EVT_COMMAND(wxID_ANY, hoxEVT_CONNECTION_RESPONSE, hoxChesscapePlayer::OnConnectionResponse)
END_EVENT_TABLE()

//-----------------------------------------------------------------------------
// hoxChesscapePlayer
//-----------------------------------------------------------------------------

hoxChesscapePlayer::hoxChesscapePlayer()
{
    wxFAIL_MSG( "This default constructor is never meant to be used." );
}

hoxChesscapePlayer::hoxChesscapePlayer( const wxString& name,
                          hoxPlayerType   type,
                          int             score )
            : hoxLocalPlayer( name, type, score )
{ 
    const char* FNAME = "hoxChesscapePlayer::hoxChesscapePlayer";
    wxLogDebug("%s: ENTER.", FNAME);
}

hoxChesscapePlayer::~hoxChesscapePlayer() 
{
    const char* FNAME = "hoxChesscapePlayer::~hoxChesscapePlayer";
    wxLogDebug("%s: ENTER.", FNAME);
}

hoxResult 
hoxChesscapePlayer::ConnectToNetworkServer( wxEvtHandler* sender )
{
    this->StartConnection();

    hoxRequest* request = new hoxRequest( hoxREQUEST_TYPE_CONNECT, sender );
	request->parameters["pid"] = this->GetName();
	request->parameters["password"] = this->GetPassword();
    //request->content = 
    //    wxString::Format("op=CONNECT&pid=%s\r\n", this->GetName().c_str());
    this->AddRequestToConnection( request );

    return hoxRESULT_OK;
}

hoxResult 
hoxChesscapePlayer::DisconnectFromNetworkServer( wxEvtHandler* sender )
{
    hoxRequest* request = new hoxRequest( hoxREQUEST_TYPE_DISCONNECT, sender );
	request->parameters["pid"] = this->GetName();
	this->AddRequestToConnection( request );

	return hoxRESULT_OK;
}

hoxResult 
hoxChesscapePlayer::QueryForNetworkTables( wxEvtHandler* sender )
{
	const char* FNAME = "hoxChesscapePlayer::QueryForNetworkTables";
	wxLogDebug("%s: ENTER.", FNAME);

	/* Just return the "cache" list. */

	hoxRequestType requestType = hoxREQUEST_TYPE_LIST;
    hoxResponse_AutoPtr response( new hoxResponse(requestType, 
                                                  sender) );
	/* Clone the "cache" list and return the cloned */
	hoxNetworkTableInfoList* pTableList = new hoxNetworkTableInfoList;

	for ( hoxNetworkTableInfoList::const_iterator it = m_networkTables.begin();
		                                          it != m_networkTables.end(); 
												++it )
	{
		pTableList->push_back( (*it) );
	}
	
	response->eventObject = pTableList;

    wxCommandEvent event( hoxEVT_CONNECTION_RESPONSE, requestType );
    response->code = hoxRESULT_OK;
    event.SetEventObject( response.release() );  // Caller will de-allocate.
    wxPostEvent( sender, event );

	return hoxRESULT_OK;
}

hoxResult 
hoxChesscapePlayer::JoinNetworkTable( const wxString& tableId,
                                      wxEvtHandler*   sender )
{
	m_pendingJoinTableId = tableId;

    hoxRequest* request = new hoxRequest( hoxREQUEST_TYPE_JOIN, sender );
	request->parameters["pid"] = this->GetName();
	request->parameters["tid"] = tableId;
    this->AddRequestToConnection( request );

    return hoxRESULT_OK;
}

void
hoxChesscapePlayer::OnIncomingNetworkData( wxSocketEvent& event )
{
    const char* FNAME = "hoxChesscapePlayer::OnIncomingNetworkData";
    wxLogDebug("%s: ENTER.", FNAME);

    hoxRequest* request = new hoxRequest( hoxREQUEST_TYPE_PLAYER_DATA, this );
    request->socket      = event.GetSocket();
    request->socketEvent = event.GetSocketEvent();
    this->AddRequestToConnection( request );
}

void 
hoxChesscapePlayer::OnConnectionResponse_PlayerData( wxCommandEvent& event )
{
    const char* FNAME = "hoxChesscapePlayer::OnConnectionResponse_PlayerData";
    hoxResult result = hoxRESULT_OK;

    wxLogDebug("%s: ENTER.", FNAME);

    hoxResponse* response_raw = wx_reinterpret_cast(hoxResponse*, event.GetEventObject());
    const std::auto_ptr<hoxResponse> response( response_raw ); // take care memory leak!

    /* Make a note to 'self' that one request has been serviced. */
    DecrementOutstandingRequests();

    /* NOTE: Only handle the connection-lost event. */

    if ( (response->flags & hoxRESPONSE_FLAG_CONNECTION_LOST) !=  0 )
    {
        wxLogDebug("%s: Connection has been lost.", FNAME);
        /* Currently, we support one connection per player.
         * Since this ONLY connection is closed, the player must leave
         * all tables.
         */
        this->LeaveAllTables();
    }
    else
    {
		wxString command;
		wxString paramsStr;

		/* Parse for the command */

		if ( ! _ParseIncomingCommand( response->content, command, paramsStr ) ) // failed?
		{
			wxLogDebug("%s: *** WARN *** Failed to parse incoming command.", FNAME);
			goto exit_label;
		}

		/* Processing the command... */

		if ( command == "show" )
		{
			wxLogDebug("%s: Processing SHOW command...", FNAME);
			wxString delims;
			delims += 0x11;   // table-delimiter
			// ... Do not return empty tokens
			wxStringTokenizer tkz( paramsStr, delims, wxTOKEN_STRTOK );
			wxString tableStr;
			while ( tkz.HasMoreTokens() )
			{
				tableStr = tkz.GetNextToken();
				_AddTableToList( tableStr );
			}
		}
		else if ( command == "unshow" )
		{
			const wxString tableId = paramsStr.BeforeFirst(0x10);
			wxLogDebug("%s: Processing UNSHOW [%s] command...", FNAME, tableId.c_str());
			if ( ! _RemoveTableFromList( tableId ) ) // not found?
			{
				wxLogDebug("%s: *** WARN *** Table [%s] to be deleted NOT FOUND.", FNAME, tableId.c_str());
			}
		}
		else if ( command == "update" )
		{
			wxString updateCmd = paramsStr.BeforeFirst(0x10);

			// It is a table update if the sub-command starts with a number.
			long nTableId = 0;
			if ( updateCmd.ToLong( &nTableId ) && nTableId > 0 )  // a table's update?
			{
				wxString tableStr = paramsStr;
				//wxLogDebug("%s: Processing UPDATE-(table) [%s] command...", FNAME, tableStr.c_str());
				_UpdateTableInList( tableStr );
			}
		}
		else if ( command == "tCmd" )
		{
			wxString tCmd = paramsStr.BeforeFirst(0x10);
			wxLogDebug("%s: Processing tCmd=[%s] command...", FNAME, tCmd.c_str());
			
			const wxString cmdStr = paramsStr.AfterFirst(0x10);

			if ( tCmd == "Settings" )
			{
				_HandleTableCmd_Settings( cmdStr );
				goto exit_label;
			}

			/* TODO: Only support 1 table for now. */
			const hoxRoleList roles = this->GetRoles();
			if ( roles.empty() )
			{
				wxLogDebug("%s: *** WARN *** This player [%s] has not joined any table yet.", 
					FNAME, this->GetName().c_str());
				goto exit_label;
			}
			wxString tableId = roles.front().tableId;

			// Find the table hosted on this system using the specified table-Id.
			hoxTable* table = this->GetSite()->FindTable( tableId );
			if ( table == NULL )
			{
				wxLogDebug("%s: *** WARN *** Table [%s] not found.", FNAME, tableId.c_str());
				goto exit_label;
			}

			if ( tCmd == "MvPts" )
			{
				_HandleTableCmd_PastMoves( table, cmdStr );
			}
			else if ( tCmd == "Move" )
			{
				_HandleTableCmd_Move( table, cmdStr );
			}
			else
			{
				wxLogDebug("%s: *** Ignore this Table-command = [%s].", FNAME, tCmd.c_str());
			}
		}
		else
		{
			wxLogDebug("%s: Ignore other command = [%s] for now.", FNAME, command.c_str());
		}
    }

exit_label:
    wxLogDebug("%s: END.", FNAME);
}

bool 
hoxChesscapePlayer::_ParseTableInfoString( const wxString&      tableStr,
	                                       hoxNetworkTableInfo& tableInfo ) const
{
	const char* FNAME = "hoxChesscapePlayer::_ParseTableInfoString";
	wxString delims;
	delims += 0x10;
	// ... Do not return empty tokens
	wxStringTokenizer tkz( tableStr, delims, wxTOKEN_STRTOK );
	int tokenPosition = 0;
	wxString token;
	wxString debugStr;  // For Debug purpose only!

	tableInfo.Clear();

	while ( tkz.HasMoreTokens() )
	{
		token = tkz.GetNextToken();
		debugStr << '[' << token << "]";
		switch (tokenPosition)
		{
			case 0: /* Table-Id */
				tableInfo.id = token; 
				break;

			case 1: /* Group: Public / Private */
				tableInfo.status = (token == "0" ? 0 : 1); 
				break;

			case 2: /* Timer symbol 'T' */
				if ( token != "T" )
					wxLogDebug("%s: *** WARN *** This token [%s] should be 'T].", FNAME, token.c_str());
				break;

			case 3: /* Timer: Game-time (in seconds) */
				tableInfo.nInitialTime = ::atoi(token.c_str()) / 1000;
				break;

			case 4: /* Timer: Increment-time (in seconds) */
				break;

			case 5: /* Table-type: Rated / Nonrated / Solo-Black / Solo-Red */
				if      ( token == "0" ) tableInfo.gameType = hoxGAME_TYPE_RATED;
				else if ( token == "1" ) tableInfo.gameType = hoxGAME_TYPE_NONRATED;
				else if ( token == "5" ) tableInfo.gameType = hoxGAME_TYPE_SOLO_BLACK;
				else if ( token == "8" ) tableInfo.gameType = hoxGAME_TYPE_SOLO_RED;
				else /* unknown */       tableInfo.gameType = hoxGAME_TYPE_UNKNOWN;
				break;

			case 6: /* Players-info */
			{
				wxString playersInfo = token;
				wxString delims;
				delims += 0x20;
				// ... Do not return empty tokens
				wxStringTokenizer tkz( playersInfo, delims, wxTOKEN_STRTOK );
				int pPosition = 0;
				wxString ptoken;
				while ( tkz.HasMoreTokens() )
				{
					token = tkz.GetNextToken();
					switch (pPosition)
					{
						case 0:	tableInfo.redId = token;   break;
						case 1:	tableInfo.redScore = token; break;
						case 2:	tableInfo.blackId = token; break;
						case 3:	tableInfo.blackScore = token; break;
						default:                           break;
					}
					++pPosition;
				}
				break;
			}

			default: /* Ignore the rest. */ break;
		}
		++tokenPosition;
	}		
	wxLogDebug("%s: ... %s", FNAME, debugStr.c_str());

	/* Do special adjustment for Solo-typed games */
	if ( tableInfo.gameType == hoxGAME_TYPE_SOLO_BLACK )
	{
		tableInfo.blackId = tableInfo.redId;
		tableInfo.redId = "COMPUTER";
		tableInfo.blackScore = tableInfo.redScore;
		tableInfo.redScore = "0";
	}
	else if ( tableInfo.gameType == hoxGAME_TYPE_SOLO_RED )
	{
		tableInfo.blackId = "COMPUTER";
	}

	return true;
}

bool 
hoxChesscapePlayer::_AddTableToList( const wxString& tableStr ) const
{
	const char* FNAME = "hoxChesscapePlayer::_AddTableToList";
	hoxNetworkTableInfo tableInfo;

	if ( ! _ParseTableInfoString( tableStr,
	                              tableInfo ) )  // failed to parse?
	{
		wxLogDebug("%s: Failed to parse table-string [%s].", FNAME, tableStr.c_str());
		return false;
	}

	/* Insert into our list. */
	m_networkTables.push_back( tableInfo );

	return true;
}

bool 
hoxChesscapePlayer::_RemoveTableFromList( const wxString& tableId ) const
{
	const char* FNAME = "hoxChesscapePlayer::_RemoveTableFromList";

	for ( hoxNetworkTableInfoList::const_iterator it = m_networkTables.begin();
		                                          it != m_networkTables.end(); 
												++it )
	{
		if ( it->id == tableId )
		{
			m_networkTables.erase( it );
			return true;
		}
	}

	return false; // not found.
}

bool 
hoxChesscapePlayer::_UpdateTableInList( const wxString& tableStr ) const
{
	const char* FNAME = "hoxChesscapePlayer::_UpdateTableInList";
	hoxNetworkTableInfo tableInfo;

	if ( ! _ParseTableInfoString( tableStr,
	                              tableInfo ) )  // failed to parse?
	{
		wxLogDebug("%s: Failed to parse table-string [%s].", FNAME, tableStr.c_str());
		return false;
	}

	/* Find the table from our list. */

	hoxNetworkTableInfoList::iterator found_it = m_networkTables.end();

	for ( hoxNetworkTableInfoList::iterator it = m_networkTables.begin();
		                                    it != m_networkTables.end(); 
								          ++it )
	{
		if ( it->id == tableInfo.id )
		{
			found_it = it;
			break;
		}
	}

	/* If the table is not found, insert into our list. */
	if ( found_it == m_networkTables.end() ) // not found?
	{
		wxLogDebug("%s: Insert a new table [%s].", FNAME, tableInfo.id.c_str());
		m_networkTables.push_back( tableInfo );
	}
	else // found?
	{
		wxLogDebug("%s: Update existing table [%s] with new info.", FNAME, tableInfo.id.c_str());
		*found_it = tableInfo;
	}

	return true; // everything is fine.
}

bool 
hoxChesscapePlayer::_ParseIncomingCommand( const wxString& contentStr,
										   wxString&       command,
										   wxString&       paramsStr ) const
{
	const char* FNAME = "hoxChesscapePlayer::_ParseIncomingCommand";

	/* CHECK: The first character must be 0x10 */
	if ( contentStr.empty() || contentStr[0] != 0x10 )
	{
		wxLogDebug("%s: *** WARN *** Invalid command = [%s].", FNAME, contentStr.c_str());
		return false;
	}

	/* Chop off the 1st character */
	const wxString actualContent = contentStr.Mid(1);

	/* Extract the command and its parameters-string */
	command = actualContent.BeforeFirst( '=' );
	paramsStr = actualContent.AfterFirst('=');

	return true;  // success
}

bool 
hoxChesscapePlayer::_HandleLoginCmd( const wxString& cmdStr,
                                     wxString&       name,
	                                 wxString&       score,
	                                 wxString&       role ) const
{
    const char* FNAME = "hoxChesscapePlayer::_HandleLoginCmd";
    wxLogDebug("%s: ENTER.", FNAME);

	wxString delims;
	delims += 0x10;
	wxStringTokenizer tkz( cmdStr, delims, wxTOKEN_STRTOK ); // No empty tokens
	int tokenPosition = 0;
	wxString token;
	while ( tkz.HasMoreTokens() )
	{
		token = tkz.GetNextToken();
		switch (tokenPosition++)
		{
			case 0: name = token; break;
			case 1: score = token; break;
			case 2: role = token; break;
			default: /* Ignore the rest. */ break;
		}
	}		

	wxLogDebug("%s: .... name=[%s], score=[%s], role=[%s].", 
		FNAME, name.c_str(), score.c_str(), role.c_str());

	return true;
}

bool 
hoxChesscapePlayer::_HandleTableCmd_Settings( const wxString& cmdStr )
{
	const char* FNAME = "hoxChesscapePlayer::_HandleTableCmd_Settings";
	wxString delims;
	delims += 0x10;
	wxStringTokenizer tkz( cmdStr, delims, wxTOKEN_STRTOK ); // No empty tokens
	wxString token;
	int tokenPosition = 0;
	long nTotalGameTime = 0;
	long nIncrementGameTime = 0;
	long nRedGameTime = 0;
	long nBlackGameTime = 0;

	while ( tkz.HasMoreTokens() )
	{
		token = tkz.GetNextToken();

		switch ( tokenPosition++ )
		{
			case 0: /* Ignore for now */ break;
			case 1: /* Ignore for now */ break;
			case 2:
			{
				if ( token != "T" )
					wxLogDebug("%s: *** WARN *** This token [%s] should be 'T].", FNAME, token.c_str());
				break;
			}

			case 3: /* Total game-time */
			{
				if ( ! token.ToLong( &nTotalGameTime ) || nTotalGameTime <= 0 )
				{
					wxLogDebug("%s: *** WARN *** Failed to parse TOTAL game-time [%s].", FNAME, token.c_str());
				}
				break;
			}

			case 4: /* Increment game-time */
			{
				// NOTE: Increment time can be 0.
				if ( ! token.ToLong( &nIncrementGameTime ) /*|| nIncrementGameTime <= 0*/ )
				{
					wxLogDebug("%s: *** WARN *** Failed to parse INCREMENT game-time [%s].", FNAME, token.c_str());
				}
				break;
			}
			case 5: /* Ignore Red player's name */ break;
			case 6:
			{
				if ( ! token.ToLong( &nRedGameTime ) || nRedGameTime <= 0 )
				{
					wxLogDebug("%s: *** WARN *** Failed to parse RED game-time [%s].", FNAME, token.c_str());
				}
				break;
			}
			case 7: /* Ignore Red player's name */ break;
			case 8:
			{
				if ( ! token.ToLong( &nBlackGameTime ) || nBlackGameTime <= 0 )
				{
					wxLogDebug("%s: *** WARN *** Failed to parse BLACK game-time [%s].", FNAME, token.c_str());
				}
				break;
			}

			default:
				/* Ignore the rest */ break;
		};
	}

	wxLogDebug("%s: Game-times for table [%s] = [%ld][%ld][%ld][%ld].", 
		FNAME, m_pendingJoinTableId.c_str(), nTotalGameTime, nIncrementGameTime,
		nRedGameTime, nBlackGameTime);

	/* Set the game times. 
	 * TODO: Ignore INCREMENT game-time for now.
	 */
	if ( nRedGameTime > 0 && nBlackGameTime > 0 )
	{
		for ( hoxNetworkTableInfoList::const_iterator it = m_networkTables.begin();
													  it != m_networkTables.end(); 
													++it )
		{
			if ( it->id == this->m_pendingJoinTableId )
			{
				hoxNetworkTableInfo* tableInfo = new hoxNetworkTableInfo( *it );
				tableInfo->nBlackGameTime = (int) (nBlackGameTime / 1000); // convert to seconds
				tableInfo->nRedGameTime = (int) (nRedGameTime / 1000);

				wxEvtHandler* sender = this->GetSite()->GetResponseHandler();
				hoxRequestType requestType = hoxREQUEST_TYPE_JOIN;
				hoxResponse_AutoPtr response( new hoxResponse(requestType, 
															  sender) );
				response->eventObject = tableInfo;

				wxCommandEvent event( hoxEVT_CONNECTION_RESPONSE, requestType );
				response->code = hoxRESULT_OK;
				event.SetEventObject( response.release() );  // Caller will de-allocate.
				wxPostEvent( sender, event );
				break;
			}
		}
	}

	return true;
}

bool 
hoxChesscapePlayer::_HandleTableCmd_PastMoves( hoxTable*       table,
	                                           const wxString& cmdStr )
{
	const char* FNAME = "hoxChesscapePlayer::_HandleTableCmd_PastMoves";

	wxString delims;
	delims += 0x10;   // move-delimiter
	wxStringTokenizer tkz( cmdStr, delims, wxTOKEN_STRTOK ); // No empty tokens
	wxString moveStr;
	while ( tkz.HasMoreTokens() )
	{
		moveStr = tkz.GetNextToken();
		wxLogDebug("%s: .... move-str=[%s].", FNAME, moveStr.c_str());
		// Inform our table...
		table->OnMove_FromNetwork( this, moveStr );
	}

	return true;
}

bool 
hoxChesscapePlayer::_HandleTableCmd_Move( hoxTable*       table,
	                                      const wxString& cmdStr )
{
	const char* FNAME = "hoxChesscapePlayer::_HandleTableCmd_Move";
	wxString moveStr;
	wxString moveParam;

	/* Parse the command-string for the new Move */

	wxString delims;
	delims += 0x10;   // move-delimiter
	wxStringTokenizer tkz( cmdStr, delims, wxTOKEN_STRTOK ); // No empty tokens
	int tokenPosition = 0;
	wxString token;

	while ( tkz.HasMoreTokens() )
	{
		token = tkz.GetNextToken();
		switch ( tokenPosition++ )
		{
			case 0: /* Move-str */
				moveStr = token; break;

			case 1: /* Move-parameter */
				moveParam = token;	break;

			default: /* Ignore the rest. */ break;
		}
	}

	/* Inform the table of the new Move 
	 * NOTE: Regarding the Move- parameter, I do not know what it is yet.
	 */

	wxLogDebug("%s: Inform table of Move = [%s][%s].", FNAME, moveStr.c_str(), moveParam.c_str());
	table->OnMove_FromNetwork( this, moveStr );

	return true;
}

void 
hoxChesscapePlayer::OnConnectionResponse( wxCommandEvent& event )
{
    const char* FNAME = "hoxChesscapePlayer::OnConnectionResponse";
    wxLogDebug("%s: ENTER.", FNAME);

    hoxResponse* response_raw = wx_reinterpret_cast(hoxResponse*, event.GetEventObject());
    std::auto_ptr<hoxResponse> response( response_raw ); // take care memory leak!

	switch ( response->type )
	{
		case hoxREQUEST_TYPE_CONNECT:
		{
			this->DecrementOutstandingRequests();
			wxLogDebug("%s: CONNECT (or LOGIN) 's response received.", FNAME);
			wxLogDebug("%s: ... response = [%s].", FNAME, response->content.c_str());

			wxString command;
			wxString paramsStr;

			if ( ! _ParseIncomingCommand( response->content, command, paramsStr ) ) // failed?
			{
				wxLogDebug("%s: *** WARN *** Failed to parse incoming command.", FNAME);
				response->code = hoxRESULT_ERR;
				response->content = "Failed to parse incoming command.";
				break;
			}

			if ( command == "code" )
			{
				wxLogDebug("%s: *** WARN *** LOGIN return code = [%s].", FNAME, paramsStr.c_str());
				response->code = hoxRESULT_ERR;
				response->content.Printf("LOGIN returns code = [%s].", paramsStr.c_str());
			}
			else
			{
				wxString       name;
				wxString       score;
				wxString       role;

				this->_HandleLoginCmd( paramsStr, name, score, role );
				response->code = hoxRESULT_OK;
				response->content.Printf("LOGIN is OK. name=[%s], score=[%s], role=[%s]", 
					name.c_str(), score.c_str(), role.c_str());
			}
			break;
		}

		/* For JOIN, lookup the tableInfo and return it. */
		case hoxREQUEST_TYPE_JOIN:
		{
			/* NOTE: This command is not done yet. 
			 * We still need to wait for server's response about the JOIN.
			 */
			const wxString tableId = response->content;
			wxASSERT_MSG(tableId == this->m_pendingJoinTableId, "The table-Ids should match.");
			break;
		}

		case hoxREQUEST_TYPE_LEAVE:
		{
			this->DecrementOutstandingRequests();
			wxLogDebug("%s: LEAVE (table) 's response received. END.", FNAME);
			break;
		}
		case hoxREQUEST_TYPE_OUT_DATA:
		{
			this->DecrementOutstandingRequests();
			wxLogDebug("%s: OUT_DATA 's response received. END.", FNAME);
			break;
		}
		case hoxREQUEST_TYPE_DISCONNECT:
		{
			this->DecrementOutstandingRequests();
			wxLogDebug("%s: DISCONNECT 's response received. END.", FNAME);
			break;
		}
		default:
			this->DecrementOutstandingRequests();
			wxLogDebug("%s: *** WARN *** Unsupported request-type [%s].", 
				FNAME, hoxUtility::RequestTypeToString(response->type));
			break;
	} // switch


	/* Post event to the sender if it is THIS player */

    if ( response->sender && response->sender != this )
    {
        wxEvtHandler* sender = response->sender;
        response.release();
        wxPostEvent( sender, event );
    }

    wxLogDebug("%s: The response is OK.", FNAME);
}

/************************* END OF FILE ***************************************/