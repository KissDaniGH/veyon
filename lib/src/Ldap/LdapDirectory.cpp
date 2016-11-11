/*
 * KLdapIntegration.h - definition of logging category for kldap
 *
 * Copyright (c) 2016 Tobias Doerffel <tobydox/at/users/dot/sf/dot/net>
 *
 * This file is part of iTALC - http://italc.sourceforge.net
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

#include <QDebug>

#include "LdapDirectory.h"

#include "ItalcCore.h"
#include "ItalcConfiguration.h"

#include "ldapconnection.h"
#include "ldapoperation.h"
#include "ldapserver.h"
#include "ldapdn.h"


class LdapDirectory::LdapDirectoryPrivate
{
public:

	QStringList queryEntries(const QString &dn, const QString &attribute,
							 const QString &filter, KLDAP::LdapUrl::Scope scope = KLDAP::LdapUrl::One )
	{
		QStringList entries;

		int id = operation.search( KLDAP::LdapDN( dn ), scope, filter, QStringList( attribute ) );

		if( id != -1 )
		{
			while( operation.waitForResult(id) == KLDAP::LdapOperation::RES_SEARCH_ENTRY )
			{
				for( auto value : operation.object().values( attribute ) )
				{
					entries += value;
				}
			}
		}
		else
		{
			qWarning() << "LDAP search failed:" << ldapErrorDescription();
		}

		return entries;
	}

	QString ldapErrorDescription() const
	{
		QString errorString = connection.ldapErrorString();
		if( errorString.isEmpty() == false )
		{
			return tr( "LDAP error description: %1" ).arg( errorString );
		}

		return QString();
	}


	KLDAP::LdapConnection connection;
	KLDAP::LdapOperation operation;

	QString baseDn;
	QString namingContextAttribute;
	QString usersDn;
	QString groupsDn;
	QString computersDn;
	QString computerPoolsDn;

	QString userLoginAttribute;

	bool isConnected;
	bool isBound;
};



LdapDirectory::LdapDirectory() :
	d( new LdapDirectoryPrivate )
{
	reconnect();
}



LdapDirectory::~LdapDirectory()
{
}



bool LdapDirectory::isConnected() const
{
	return d->isConnected;
}



bool LdapDirectory::isBound() const
{
	return d->isBound;
}



QString LdapDirectory::ldapErrorDescription() const
{
	return d->ldapErrorDescription();
}



QStringList LdapDirectory::queryEntries(const QString &dn, const QString &attribute, const QString &filter)
{
	return d->queryEntries( dn, attribute, filter );
}



QStringList LdapDirectory::queryBaseDn(const QString &attribute)
{
	return d->queryEntries( d->baseDn, attribute, QString(), KLDAP::LdapUrl::Base );
}



QString LdapDirectory::queryNamingContext()
{
	QStringList namingContextEntries = d->queryEntries( QString(), d->namingContextAttribute, QString(), KLDAP::LdapUrl::Base );

	if( namingContextEntries.isEmpty() )
	{
		return QString();
	}

	return namingContextEntries.first();
}



QStringList LdapDirectory::users(const QString &filter)
{
	QString queryFilter;

	if( filter.isEmpty() == false )
	{
		queryFilter = QString( "(%1=%2)" ).arg( d->userLoginAttribute ).arg( filter );
	}

	return d->queryEntries( d->usersDn, d->userLoginAttribute, queryFilter );
}



QStringList LdapDirectory::groups(const QString &filter)
{
	return d->queryEntries( d->groupsDn, "cn", filter );
}



QStringList LdapDirectory::computers(const QString &filter)
{
	return d->queryEntries( d->computersDn, "cn", filter );
}



QStringList LdapDirectory::computerPools(const QString &filter)
{
	return d->queryEntries( d->computerPoolsDn, "cn", filter );
}



bool LdapDirectory::reconnect()
{
	ItalcConfiguration* c = ItalcCore::config;

	KLDAP::LdapServer server;

	server.setHost( c->ldapServerHost() );
	server.setPort( c->ldapServerPort() );
	server.setBaseDn( KLDAP::LdapDN( c->ldapBaseDn() ) );

	if( c->ldapUseBindCredentials() )
	{
		server.setBindDn( c->ldapBindDn() );
		server.setPassword( c->ldapBindPassword() );
		server.setAuth( KLDAP::LdapServer::Simple );
	}
	else
	{
		server.setAuth( KLDAP::LdapServer::Anonymous );
	}
	server.setSecurity( KLDAP::LdapServer::None );

	d->connection.close();

	d->isConnected = false;
	d->isBound = false;

	d->connection.setServer( server );
	if( d->connection.connect() != 0 )
	{
		qWarning() << "LDAP connect failed:" << ldapErrorDescription();
		return false;
	}

	d->isConnected = true;

	d->operation.setConnection( d->connection );
	if( d->operation.bind_s() != 0 )
	{
		qWarning() << "LDAP bind failed:" << ldapErrorDescription();
		return false;
	}

	d->isBound = true;

	d->namingContextAttribute = c->ldapNamingContextAttribute();

	if( d->namingContextAttribute.isEmpty() )
	{
		// fallback to AD default value
		d->namingContextAttribute = "defaultNamingContext";
	}

	// query base DN via naming context if configured
	if( c->ldapQueryNamingContext() )
	{
		d->baseDn = queryNamingContext();
	}
	else
	{
		// use the configured base DN
		d->baseDn = c->ldapBaseDn();
	}

	d->usersDn = c->ldapUserTree() + "," + c->ldapBaseDn();
	d->groupsDn = c->ldapGroupTree() + "," + c->ldapBaseDn();
	d->computersDn = c->ldapComputerTree() + "," + c->ldapBaseDn();
	d->computerPoolsDn = c->ldapComputerPoolTree() + "," + c->ldapBaseDn();

	d->userLoginAttribute = c->ldapUserLoginAttribute();

	return true;
}