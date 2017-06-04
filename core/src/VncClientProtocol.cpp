/*
 * VncClientProtocol.cpp - implementation of the VncClientProtocol class
 *
 * Copyright (c) 2017 Tobias Doerffel <tobydox/at/users/dot/sf/dot/net>
 *
 * This file is part of Veyon - http://veyon.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#include "VeyonCore.h"

#include <QTcpSocket>

extern "C"
{
#include "rfb/rfbproto.h"
#include "common/d3des.h"
}

#include "VncClientProtocol.h"


/*
 * Encrypt CHALLENGESIZE bytes in memory using a password.
 */

static void
vncEncryptBytes(unsigned char *bytes, char *passwd)
{
	unsigned char key[8];
	unsigned int i;

	/* key is simply password padded with nulls */

	for (i = 0; i < 8; i++) {
		if (i < strlen(passwd)) {
			key[i] = passwd[i];
		} else {
			key[i] = 0;
		}
	}

	rfbDesKey(key, EN0);

	for (i = 0; i < CHALLENGESIZE; i += 8) {
		rfbDes(bytes+i, bytes+i);
	}
}




VncClientProtocol::VncClientProtocol( QTcpSocket* socket, const QString& vncPassword ) :
	m_socket( socket ),
	m_state( Disconnected ),
	m_vncPassword( vncPassword.toLatin1() ),
	m_serverInitMessage()
{
	memset( &m_pixelFormat, 0, sz_rfbPixelFormat );
}



void VncClientProtocol::start()
{
	m_state = Protocol;
}



bool VncClientProtocol::read()
{
	switch( m_state )
	{
	case Protocol:
		return readProtocol();

	case SecurityInit:
		return receiveSecurityTypes();

	case SecurityChallenge:
		return receiveSecurityChallenge();

	case SecurityResult:
		return receiveSecurityResult();

	case FramebufferInit:
		return receiveServerInitMessage();

	default:
		break;
	}

	return false;
}



bool VncClientProtocol::readProtocol()
{
	if( m_socket->bytesAvailable() == sz_rfbProtocolVersionMsg )
	{
		char protocol[sz_rfbProtocolVersionMsg+1];
		m_socket->read( protocol, sz_rfbProtocolVersionMsg );
		protocol[sz_rfbProtocolVersionMsg] = 0;

		int protocolMajor = 0, protocolMinor = 0;

		if( sscanf( protocol, rfbProtocolVersionFormat, &protocolMajor, &protocolMinor ) != 2 ||
				protocolMajor != 3 || protocolMinor < 7 )
		{
			qCritical( "VncClientProtocol:::readProtocol(): protocol initialization failed" );
			m_socket->close();

			return false;
		}

		m_socket->write( protocol, sz_rfbProtocolVersionMsg );

		m_state = SecurityInit;

		return true;
	}

	return false;
}



bool VncClientProtocol::receiveSecurityTypes()
{
	if( m_socket->bytesAvailable() >= 2 )
	{
		char securityTypeCount = 0;

		m_socket->read( &securityTypeCount, sizeof(securityTypeCount) );

		if( securityTypeCount == 0 )
		{
			qCritical( "VncClientProtocol::receiveSecurityTypes(): invalid number of security types received!" );
			m_socket->close();

			return false;
		}

		QByteArray securityTypeList = m_socket->read( securityTypeCount );
		if( securityTypeList.count() != securityTypeCount )
		{
			qCritical( "VncClientProtocol::receiveSecurityTypes(): could not read security types!" );
			m_socket->close();

			return false;
		}

		char securityType = rfbSecTypeVncAuth;

		if( securityTypeList.contains( securityType ) == false )
		{
			qCritical( "VncClientProtocol::receiveSecurityTypes(): no supported security type!" );
			m_socket->close();

			return false;
		}

		m_socket->write( &securityType, sizeof(securityType) );

		m_state = SecurityChallenge;

		return true;
	}

	return false;
}



bool VncClientProtocol::receiveSecurityChallenge()
{
	if( m_socket->bytesAvailable() >= CHALLENGESIZE )
	{
		uint8_t challenge[CHALLENGESIZE];
		m_socket->read( (char *) challenge, CHALLENGESIZE );

		vncEncryptBytes( challenge, m_vncPassword.data() );

		m_socket->write( (const char *) challenge, CHALLENGESIZE );

		m_state = SecurityResult;

		return true;
	}

	return false;
}



bool VncClientProtocol::receiveSecurityResult()
{
	if( m_socket->bytesAvailable() >= 4 )
	{
		uint32_t authResult = 0;

		m_socket->read( (char *) &authResult, sizeof(authResult) );

		if( qFromBigEndian( authResult ) != rfbVncAuthOK )
		{
			qCritical( "VncClientProtocol::receiveSecurityResult(): authentication failed!" );
			m_socket->close();
			return false;
		}

		qDebug( "VncClientProtocol::receiveSecurityResult(): authentication successful" );

		// finally send client init message
		rfbClientInitMsg clientInitMessage;
		clientInitMessage.shared = 1;
		m_socket->write( (const char *) &clientInitMessage, sz_rfbClientInitMsg );

		// wait for server init message
		m_state = FramebufferInit;

		return true;
	}

	return false;
}



bool VncClientProtocol::receiveServerInitMessage()
{
	rfbServerInitMsg message;

	if( m_socket->bytesAvailable() >= sz_rfbServerInitMsg &&
			m_socket->peek( (char *) &message, sz_rfbServerInitMsg ) == sz_rfbServerInitMsg )
	{
		auto nameLength = qFromBigEndian( message.nameLength );

		if( nameLength > 255 )
		{
			qCritical() << Q_FUNC_INFO << "size of desktop name > 255!";
			m_socket->close();
			return false;
		}

		memcpy( &m_pixelFormat, &message.format, sz_rfbPixelFormat );

		if( static_cast<uint32_t>( m_socket->peek( nameLength ).size() ) == nameLength )
		{
			m_serverInitMessage = m_socket->read( sz_rfbServerInitMsg + nameLength );

			m_state = Running;

			return true;
		}
	}

	return false;
}