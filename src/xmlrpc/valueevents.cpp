/* 
 * Value changes triggering infrastructure. 
 * Copyright (C) 2009 Petr Kubanek <petr@kubanek.net>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include "xmlrpcd.h"

#include "../utils/connfork.h"

using namespace rts2xmlrpc;

ValueChange::ValueChange (XmlRpcd *_master, std::string _deviceName, std::string _valueName, float _cadency):Rts2Object ()
{
	master = _master;
	deviceName = _deviceName;
	valueName = _valueName;
			
	lastTime = 0;
	cadency = _cadency;

	if (cadency > 0)
		master->addTimer (cadency, new Rts2Event (EVENT_XMLRPC_VALUE_TIMER, this));
}

void ValueChange::postEvent (Rts2Event *event)
{
	switch (event->getType ())
	{
		case EVENT_XMLRPC_VALUE_TIMER:
			if (lastTime + cadency <= time(NULL))
			{
				Rts2Conn *conn = master->getOpenConnection (deviceName.c_str ());
				if (conn)
					conn->queCommand (new Rts2CommandInfo (master));
			}
			if (cadency > 0)
				master->addTimer (cadency, new Rts2Event (EVENT_XMLRPC_VALUE_TIMER, this));
			break;
	}
	Rts2Object::postEvent (event);
}

#ifndef HAVE_PGSQL

void ValueChangeRecord::run (Rts2Value *val, double validTime)
{
	std::cout << Timestamp (validTime) << " value: " << deviceName << " " << valueName << val->getDisplayValue () << std::endl;
}

#endif /* ! HAVE_PGSQL */

void ValueChangeCommand::run (Rts2Value *val, double validTime)
{
	int ret;
	rts2core::ConnFork *cf = new rts2core::ConnFork (master, commandName.c_str (), true, 100);
	cf->addArg (val->getName ());
	cf->addArg (validTime);
	ret = cf->init ();
	if (ret)
	{
		delete cf;
		return;
	}

	master->addConnection (cf);
}

void ValueChangeEmail::run (Rts2Value *val, double validTime)
{
	EmailAction::run (master, NULL, validTime);
}
