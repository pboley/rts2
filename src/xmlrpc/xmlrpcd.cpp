/* 
 * XML-RPC daemon.
 * Copyright (C) 2007-2009 Petr Kubanek <petr@kubanek.net>
 * Copyright (C) 2007 Stanislav Vitek <standa@iaa.es>
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

#ifdef HAVE_PGSQL
#include "../utilsdb/recvals.h"
#include "../utilsdb/records.h"
#include "../utilsdb/recordsavg.h"
#include "../utilsdb/rts2devicedb.h"
#include "../utilsdb/rts2imgset.h"
#include "../utilsdb/observationset.h"
#include "../utilsdb/rts2messagedb.h"
#include "../utilsdb/targetset.h"
#include "../utilsdb/rts2user.h"
#include "../utilsdb/sqlerror.h"
#include "../scheduler/ticket.h"
#include "../writers/rts2imagedb.h"
#else
#include "../utils/rts2config.h"
#include "../utils/rts2device.h"
#endif /* HAVE_PGSQL */

#if defined(HAVE_LIBJPEG) && HAVE_LIBJPEG == 1
#include <Magick++.h>
using namespace Magick;
#endif // HAVE_LIBJPEG

#include "../utils/libnova_cpp.h"
#include "../utils/timestamp.h"
#include "../utils/error.h"
#include "../writers/rts2image.h"
#include "xmlstream.h"
#include "httpreq.h"
#include "augerreq.h"

#include "r2x.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#define OPT_STATE_CHANGE        OPT_LOCAL + 76
#define OPT_NO_EMAILS           OPT_LOCAL + 77

using namespace XmlRpc;

/**
 * @file
 * XML-RPC access to RTS2.
 *
 * @defgroup XMLRPC XML-RPC
 */

/**
 * XML-RPC server.
 */
XmlRpcServer xmlrpc_server;

using namespace rts2xmlrpc;

void XmlDevClient::stateChanged (Rts2ServerState * state)
{
	((XmlRpcd *)getMaster ())->stateChangedEvent (getConnection (), state);
	Rts2DevClient::stateChanged (state);
}

void XmlDevClient::valueChanged (Rts2Value * value)
{
	((XmlRpcd *)getMaster ())->valueChangedEvent (getConnection (), value);
	Rts2DevClient::valueChanged (value);
}

#ifndef HAVE_PGSQL
int XmlRpcd::willConnect (Rts2Address *_addr)
{
	if (_addr->getType () < getDeviceType ()
		|| (_addr->getType () == getDeviceType ()
		&& strcmp (_addr->getName (), getDeviceName ()) < 0))
		return 1;
	return 0;
}
#endif

int XmlRpcd::processOption (int in_opt)
{
	switch (in_opt)
	{
		case 'p':
			rpcPort = atoi (optarg);
			break;
		case OPT_STATE_CHANGE:
			stateChangeFile = optarg;
			break;
		case OPT_NO_EMAILS:
			send_emails->setValueBool (false);
			break;
#ifdef HAVE_PGSQL
		default:
			return Rts2DeviceDb::processOption (in_opt);
#else
		case OPT_CONFIG:
			config_file = optarg;
			break;
		default:
			return Rts2Device::processOption (in_opt);
#endif
	}
	return 0;
}

int XmlRpcd::init ()
{
	int ret;
#ifdef HAVE_PGSQL
	ret = Rts2DeviceDb::init ();
#else
	ret = Rts2Device::init ();
#endif
	if (ret)
		return ret;

	if (printDebug ())
		XmlRpc::setVerbosity (5);

	xmlrpc_server.bindAndListen (rpcPort);
	xmlrpc_server.enableIntrospection (true);

	// try states..
	if (stateChangeFile != NULL)
	{
		try
		{
			events.load (stateChangeFile);
		}
		catch (XmlError ex)
		{
			logStream (MESSAGE_ERROR) << ex << sendLog;
			return -1;
		}
	}

	setMessageMask (MESSAGE_MASK_ALL);

#ifndef HAVE_PGSQL
	ret = Rts2Config::instance ()->loadFile (config_file);
	if (ret)
		return ret;
#endif
	// get page prefix
	Rts2Config::instance ()->getString ("xmlrpcd", "page_prefix", page_prefix, "");

	return ret;
}

int XmlRpcd::setValue (Rts2Value *old_value, Rts2Value *new_value)
{
	if (old_value == send_emails)
		return 0;

#ifdef HAVE_PGSQL
	return Rts2DeviceDb::setValue (old_value, new_value);
#else
	return Rts2Device::setValue (old_value, new_value);
#endif
}

void XmlRpcd::addSelectSocks ()
{
#ifdef HAVE_PGSQL
	Rts2DeviceDb::addSelectSocks ();
#else
	Rts2Device::addSelectSocks ();
#endif
	xmlrpc_server.addToFd (&read_set, &write_set, &exp_set);
}

void XmlRpcd::selectSuccess ()
{
#ifdef HAVE_PGSQL
	Rts2DeviceDb::selectSuccess ();
#else
	Rts2Device::selectSuccess ();
#endif
	xmlrpc_server.checkFd (&read_set, &write_set, &exp_set);
}

void XmlRpcd::signaledHUP ()
{
#ifdef HAVE_PGSQL
	Rts2DeviceDb::selectSuccess ();
#else
	Rts2Device::selectSuccess ();
#endif
	// try states..
	if (stateChangeFile != NULL)
	{
		events.load (stateChangeFile);
	}
}

#ifdef HAVE_PGSQL
XmlRpcd::XmlRpcd (int argc, char **argv): Rts2DeviceDb (argc, argv, DEVICE_TYPE_SOAP, "XMLRPC"), events (this)
#else
XmlRpcd::XmlRpcd (int argc, char **argv): Rts2Device (argc, argv, DEVICE_TYPE_SOAP, "XMLRPC")
#endif
{
	rpcPort = 8889;
	stateChangeFile = NULL;

	createValue (send_emails, "send_email", "if XML-RPC is allowed to send emails", false);
	send_emails->setValueBool (true);

#ifndef HAVE_PGSQL
	config_file = NULL;

	addOption (OPT_CONFIG, "config", 1, "configuration file");
#endif
	addOption ('p', NULL, 1, "XML-RPC port. Default to 8889");
	addOption (OPT_STATE_CHANGE, "event-file", 1, "event changes file, list commands which are executed on state change");
	addOption (OPT_NO_EMAILS, "no-emails", 0, "do not send emails");
	XmlRpc::setVerbosity (0);
}


XmlRpcd::~XmlRpcd ()
{
	for (std::map <std::string, Session *>::iterator iter = sessions.begin (); iter != sessions.end (); iter++)
	{
		delete (*iter).second;
	}
	sessions.clear ();
}

Rts2DevClient * XmlRpcd::createOtherType (Rts2Conn * conn, int other_device_type)
{
	return new XmlDevClient (conn);
}

void XmlRpcd::stateChangedEvent (Rts2Conn * conn, Rts2ServerState * new_state)
{
	double now = getNow ();
	// look if there is some state change command entry, which match us..
	for (StateCommands::iterator iter = events.stateCommands.begin (); iter != events.stateCommands.end (); iter++)
	{
		StateChange *sc = (*iter);
		if (sc->isForDevice (conn->getName (), conn->getOtherType ()) && sc->executeOnStateChange (new_state->getOldValue (), new_state->getValue ()))
		{
			sc->run (this, conn, now);
		}
	}
}

void XmlRpcd::valueChangedEvent (Rts2Conn * conn, Rts2Value * new_value)
{
	double now = getNow ();
	// look if there is some state change command entry, which match us..
	for (ValueCommands::iterator iter = events.valueCommands.begin (); iter != events.valueCommands.end (); iter++)
	{
		ValueChange *vc = (*iter);
		if (vc->isForValue (conn->getName (), new_value->getName (), now))
		 
		{
			try
			{
				vc->run (new_value, now);
				vc->runSuccessfully (now);
			}
			catch (rts2core::Error err)
			{
				logStream (MESSAGE_ERROR) << err << sendLog;
			}
		}
	}
}

void XmlRpcd::message (Rts2Message & msg)
{
// log message to DB, if database is present
#ifdef HAVE_PGSQL
	if (msg.isNotDebug ())
	{
		Rts2MessageDB msgDB (msg);
		msgDB.insertDB ();
	}
#endif
	while (messages.size () > 42) // messagesBufferSize ())
	{
		messages.pop_front ();
	}

	messages.push_back (msg);
}

std::string XmlRpcd::addSession (std::string _username, time_t _timeout)
{
	Session *s = new Session (_username, time(NULL) + _timeout);
	sessions[s->getSessionId()] = s;
	return s->getSessionId ();
}

bool XmlRpcd::existsSession (std::string sessionId)
{
	std::map <std::string, Session*>::iterator iter = sessions.find (sessionId);
	if (iter == sessions.end ())
	{
		return false;
	}
	return true;
}

/**
 * Return session ID for user, if login is allowed.
 *
 * @author Petr Kubanek <petr@kubanek.net>
 *
 * @addgroup XMLRPC
 */
class Login: public XmlRpcServerMethod
{
	public:
		Login (XmlRpcServer* s): XmlRpcServerMethod (R2X_LOGIN, s)
		{
		}

		void execute (XmlRpcValue& params, XmlRpcValue& result)
		{
			if (params.size () != 2)
			{
				throw XmlRpcException ("Invalid number of parameters");
			}

#ifdef HAVE_PGSQL
			if (verifyUser (params[0], params[1]) == false)
			{
				throw XmlRpcException ("Invalid login or password");
			}
#else
			if (! (params[0] ==  std::string ("petr") && params[1] == std::string ("test")))
				throw XmlRpcException ("Login not supported");
#endif /* HAVE_PGSQL */

			result = ((XmlRpcd *) getMasterApp ())->addSession (params[0], 3600);
		}

		std::string help ()
		{
			return std::string ("Return session ID for user if logged properly");
		}
} login(&xmlrpc_server);

#if defined(HAVE_LIBJPEG) && HAVE_LIBJPEG == 1

class JpegImageRequest: public GetRequestAuthorized
{
	public:
		JpegImageRequest (const char* prefix, XmlRpcServer* s):GetRequestAuthorized (prefix, s)
		{
		}

		virtual void authorizedExecute (std::string path, HttpParams *params, const char* &response_type, char* &response, int &response_length)
		{
			response_type = "image/jpeg";
			Rts2Image image (path.c_str (), false, true);
			image.openImage ();
			Blob blob;
			Magick::Image mimage = image.getMagickImage ();
			mimage.fillColor (Magick::Color (0, 0, 0));
			mimage.draw (Magick::DrawableRectangle (5, image.getHeight () - 30, image.getWidth () - 10, image.getHeight () - 5));

			mimage.fillColor (Magick::Color (MaxRGB, MaxRGB, MaxRGB));
			mimage.draw (Magick::DrawableText (10, image.getHeight () - 10, image.expand ("%Y-%m-%d %H:%M:%S @OBJECT")));

			mimage.write (&blob, "jpeg");
			response_length = blob.length();
			response = new char[response_length];
			memcpy (response, blob.data(), response_length);
		}
} jpegRequest ("/jpeg", &xmlrpc_server);

/**
 * Sort two file structure entries by cdate.
 */
#if _POSIX_C_SOURCE > 200200L
int cdatesort(const struct dirent **a, const struct dirent **b)
{
	struct stat s_a, s_b;
        if (stat ((*a)->d_name, &s_a))
                return 1;
        if (stat ((*b)->d_name, &s_b))
                return -1;
        if (s_a.st_ctime == s_b.st_ctime)
                return 0;
        if (s_a.st_ctime > s_b.st_ctime)
                return 1;
        return -1;
}
#else
int cdatesort(const void *a, const void *b)
{
	struct stat s_a, s_b;
        const struct dirent * d_a = *((dirent**)a);
        const struct dirent * d_b = *((dirent**)b);
        if (stat (d_a->d_name, &s_a))
                return 1;
        if (stat (d_b->d_name, &s_b))
                return -1;
        if (s_a.st_ctime == s_b.st_ctime)
                return 0;
        if (s_a.st_ctime > s_b.st_ctime)
                return 1;
        return -1;
}
#endif

/**
 * Create page with JPEG previews.
 *
 * @param p Page number. Default to 0.
 * @param s Page size (number of images per page). Defalt to 100.
 *
 * @author Petr Kubanek <petr@kubanek.net>
 */
class JpegPreview:public GetRequestAuthorized
{
	public:
		JpegPreview (const char* prefix, XmlRpcServer *s):GetRequestAuthorized (prefix, s) {}

		void pageLink (std::ostringstream& _os, const char* path, int i, int pagesiz, bool selected);

		virtual void authorizedExecute (std::string path, HttpParams *params, const char* &response_type, char* &response, int &response_length)
		{
			// if it is a fits file..
			if (path.length () > 6 && (path.substr (path.length () - 5)) == std::string (".fits"))
			{
				response_type = "image/jpeg";

				Rts2Image image (path.c_str (), false, true);
				image.openImage ();
				Blob blob;
				Magick::Image mimage = image.getMagickImage ();
				mimage.zoom (Magick::Geometry (128, 128));
				mimage.fillColor (Magick::Color (0, 0, 0));
				mimage.fontPointsize (10);
				mimage.draw (Magick::DrawableRectangle (0, 116, 128, 128));

				mimage.fillColor (Magick::Color (MaxRGB, MaxRGB, MaxRGB));
				mimage.draw (Magick::DrawableText (1, 126, image.expand ("%Y-%m-%d %H:%M:%S")));

				mimage.write (&blob, "jpeg");
				response_length = blob.length();
				response = new char[response_length];
				memcpy (response, blob.data(), response_length);
				return;
			}
			std::ostringstream _os;
			_os << "<html><head><title>Preview of " << path << "</title></head><body>";

			struct dirent **namelist;
			int n;

			int ret = chdir (path.c_str ());
			if (ret)
			{
			  	throw XmlRpcException ("Invalid directory");
			}

			const char *pagesort = params->getString ("o", "filename");

			enum {SORT_FILENAME, SORT_DATE} sortby = SORT_FILENAME;
			if (!strcmp (pagesort, "date"))
				sortby = SORT_DATE;

			switch (sortby)
			{
			 	case SORT_DATE:
 /* if following fails to compile, please have a loog to value of your
  * _POSIX_C_SOURCE #define, and record it and send it to petr@kubanek.net.
  * Please contact petr@kubanek.net if you don't know how to get
  * _POSIX_C_SOURCE. */
					n = scandir (".", &namelist, 0, cdatesort);	
					break;
				case SORT_FILENAME:
				default:
				  	n = scandir (".", &namelist, 0, alphasort);
					break;
			}

			if (n < 0)
			{
				throw XmlRpcException ("Cannot open directory");
			}

			// first show directories..
			_os << "<p>";
			for (int i = 0; i < n; i++)
			{
				char *fname = namelist[i]->d_name;
				struct stat sbuf;
				ret = stat (fname, &sbuf);
				if (ret)
					continue;
				if (S_ISDIR (sbuf.st_mode) && strcmp (fname, ".") != 0)
				{
					_os << "<a href='" << ((XmlRpcd *)getMasterApp ())->getPagePrefix () << "/preview" << path << fname << "/'>" << fname << "</a> ";
				}
			}

			_os << "</p><p/>\n";

			// get page number and size of page
			int pageno = params->getInteger ("p", 1);
			int pagesiz = params->getInteger ("s", 40);

			if (pageno <= 0)
				pageno = 1;

			int is = (pageno - 1) * pagesiz;
			int ie = is + pagesiz;
			int in = 0;

			int i;

			for (i = 0; i < n; i++)
			{
				char *fname = namelist[i]->d_name;
				if (strstr (fname + strlen (fname) - 6, ".fits") == NULL)
					continue;
				in++;
				if (in <= is)
					continue;
				if (in > ie)
					continue;
				std::string fpath = std::string (path) + '/' + fname;
				_os << "<a href='" << ((XmlRpcd *)getMasterApp ())->getPagePrefix () << "/jpeg" << fpath 
				  << "'><img width='128' height='128' src='" << ((XmlRpcd *)getMasterApp())->getPagePrefix () << "/preview" << fpath << "'/></a>&nbsp;";
				//<a href='/fits" << fpath
				//	<< "'>FITS</a>&nbsp;<a href='/jpeg" << fpath << "'>JPEG</a></li>";
			}

			for (i = 0; i < n; i++)
			{
				free (namelist[i]);
			}

			free (namelist);
			
			// print pages..
			_os << "<p>Page ";
			for (i = 1; i <= in / pagesiz; i++)
			 	pageLink (_os, path.c_str (), i, pagesiz, i == pageno);
			if (in % pagesiz)
			 	pageLink (_os, path.c_str (), i, pagesiz, i == pageno);
			_os << "</p></body></html>";

			response_type = "text/html";
			response_length = _os.str ().length ();
			response = new char[response_length];
			memcpy (response, _os.str ().c_str (), response_length);
		}
} jpegPreview ("/preview", &xmlrpc_server);

void JpegPreview::pageLink (std::ostringstream& _os, const char* path, int i, int pagesiz, bool selected)
{
	if (selected)
	{
		_os << "<b>" << i << "</b> ";
	}
	else
	{
		_os << "<a href='" << ((XmlRpcd *)getMasterApp ())->getPagePrefix () << "/preview" << path << "?p=" << i << "&s=" << pagesiz << "'>" << i << "</a> ";
	}
}

#endif // HAVE_LIBJPEG

class FitsImageRequest:public GetRequestAuthorized
{
	public:
		FitsImageRequest (const char* prefix, XmlRpcServer* s):GetRequestAuthorized (prefix, s)
		{
		}

		virtual void authorizedExecute (std::string path, HttpParams *params, const char* &response_type, char* &response, int &response_length)
		{
			response_type = "image/fits";
			int f = open (path.c_str (), O_RDONLY);
			if (f == -1)
			{
				throw XmlRpcException ("Cannot open file");
			}
			struct stat st;
			if (fstat (f, &st) == -1)
			{
				throw XmlRpcException ("Cannot get file properties");
			}
			response_length = st.st_size;
			response = new char[response_length];
			int ret;
			ret = read (f, response, response_length);
			if (ret != response_length)
			{
				delete[] response;
				throw XmlRpcException ("Cannot read data");
			}
			close (f);
		}
} fitsRequest ("/fits", &xmlrpc_server);

/**
 * Represents session methods. Those must be executed either with user name and
 * password, or with "session_id" passed as username and session ID passed in password.
 *
 * @author Petr Kubanek <petr@kubanek.net>
 *
 * @addgroup XMLRPC
 */
class SessionMethod: public XmlRpcServerMethod
{
	public:
		SessionMethod (const char *method, XmlRpcServer* s): XmlRpcServerMethod (method, s)
		{
		}

		void execute (XmlRpcValue& params, XmlRpcValue& result)
		{
			if (getUsername () == std::string ("session_id"))
			{
				if (((XmlRpcd *) getMasterApp ())->existsSession (getPassword ()) == false)
				{
					throw XmlRpcException ("Invalid session ID");
				}
			}

#ifdef HAVE_PGSQL
			if (verifyUser (getUsername (), getPassword ()) == false)
			{
				throw XmlRpcException ("Invalid login or password");
			}
#else
			if (! (getUsername() ==  std::string ("petr") && getPassword() == std::string ("test")))
				throw XmlRpcException ("Login not supported");
#endif /* HAVE_PGSQL */

			sessionExecute (params, result);
		}

		virtual void sessionExecute (XmlRpcValue& params, XmlRpcValue& result) = 0;
};


/**
 * Returns number of devices connected to the system.
 *
 * @author Petr Kubanek <petr@kubanek.net>
 *
 * @addgroup XMLRPC
 */
class DeviceCount: public SessionMethod
{
	public:
		DeviceCount (XmlRpcServer* s) : SessionMethod ("DeviceCount", s)
		{
		}

		void sessionExecute (XmlRpcValue& params, XmlRpcValue& result)
		{
			result = ((XmlRpcd *) getMasterApp ())->connectionSize ();
		}

		std::string help ()
		{
			return std::string ("Returns number of devices connected to XMLRPC");
		}
} deviceSum (&xmlrpc_server);

/**
 * List name of devices connected to the server.
 *
 * @author Petr Kubanek <petr@kubanek.net>
 *
 * @addgroup XMLRPC
 */
class ListDevices: public SessionMethod
{
	public:
		ListDevices (XmlRpcServer* s) : SessionMethod (R2X_DEVICES_LIST, s)
		{
		}

		void sessionExecute (XmlRpcValue& params, XmlRpcValue& result)
		{
			XmlRpcd *serv = (XmlRpcd *) getMasterApp ();
			connections_t::iterator iter;
			int i = 0;
			for (iter = serv->getConnections ()->begin (); iter != serv->getConnections ()->end (); iter++, i++)
			{
				result[i] = (*iter)->getName ();
			}
		}

		std::string help ()
		{
			return std::string ("Returns name of devices conencted to the system");
		}
} listDevices (&xmlrpc_server);


/**
 * Get device type. Returns string describing device type.
 *
 * @author Petr Kubanek <petr@kubanek.net>
 *
 * @addgroup XMLRPC
 */
class DeviceType: public SessionMethod
{
	public:
		DeviceType (XmlRpcServer* s): SessionMethod (R2X_DEVICE_TYPE, s)
		{
		}

		void sessionExecute (XmlRpcValue& params, XmlRpcValue &result)
		{
			if (params.size () != 1)
				throw XmlRpcException ("Single device name expected");
			XmlRpcd *serv = (XmlRpcd *) getMasterApp ();
			Rts2Conn *conn = serv->getOpenConnection (((std::string)params[0]).c_str());
			if (conn == NULL)
				throw XmlRpcException ("Cannot get device with name " + (std::string)params[0]);
			result = conn->getOtherType ();
		}

} deviceType (&xmlrpc_server);

/**
 * Execute command on device.
 *
 * @author Petr Kubanek <petr@kubanek.net>
 *
 * @addgroup XMLRPC
 */
class DeviceCommand: public SessionMethod
{
	public:
		DeviceCommand (XmlRpcServer* s): SessionMethod (R2X_DEVICE_COMMAND, s)
		{
		}

		void sessionExecute (XmlRpcValue& params, XmlRpcValue &result)
		{
			if (params.size () != 2)
				throw XmlRpcException ("Device name and command (as single parameter) expected");
			XmlRpcd *serv = (XmlRpcd *) getMasterApp ();
			Rts2Conn *conn = serv->getOpenConnection (((std::string)params[0]).c_str());
			if (conn == NULL)
				throw XmlRpcException ("Cannot get device with name " + (std::string)params[0]);
			conn->queCommand (new Rts2Command (serv, ((std::string)params[1]).c_str()));
		}

} deviceCommand (&xmlrpc_server);

/**
 * List device status.
 *
 * @author Petr Kubanek <petr@kubanek.net>
 *
 * @addgroup XMLRPC
 */
class DevicesStatus: public SessionMethod
{
	public:
		DevicesStatus (XmlRpcServer* s) : SessionMethod (R2X_DEVICES_STATUS, s)
		{
		}

		void sessionExecute (XmlRpcValue& params, XmlRpcValue& result)
		{
			if (params.size () != 1)
			{
				throw XmlRpcException ("Invalid number of parameters");
			}
			XmlRpcd *serv = (XmlRpcd *) getMasterApp ();
			std::string p1 = std::string (params[0]);

			Rts2Conn *conn;

			if (p1 == "centrald" || p1 == "")
				conn = *(serv->getCentraldConns ()->begin ());
			else
				conn = serv->getOpenConnection (p1.c_str ());

			if (conn == NULL)
			{
				throw XmlRpcException ("Cannot find specified device");
			}
			result[0] = conn->getStateString ();
			result[1] = conn->getRealState ();
		}

		std::string help ()
		{
			return std::string ("Returns state of devices");
		}
} devicesStatus (&xmlrpc_server);


/**
 * List name of all values accessible from server.
 *
 * @author Petr Kubanek <petr@kubanek.net>
 *
 * @addgroup XMLRPC
 */
class ListValues: public SessionMethod
{
	protected:
		ListValues (const char *in_name, XmlRpcServer* s): SessionMethod (in_name, s)
		{
		}

	public:
		ListValues (XmlRpcServer* s): SessionMethod (R2X_VALUES_LIST, s)
		{
		}

		void sessionExecute (XmlRpcValue& params, XmlRpcValue& result)
		{
			XmlRpcd *serv = (XmlRpcd *) getMasterApp ();
			Rts2Conn *conn;
			connections_t::iterator iter;
			Rts2ValueVector::iterator variter;
			int i = 0;
			// print results for single device..
			for (iter = serv->getCentraldConns ()->begin (); iter != serv->getCentraldConns ()->end (); iter++)
			{
				conn = *iter;
				std::string deviceName = (*iter)->getName ();
				// filter device list
				for (variter = conn->valueBegin (); variter != conn->valueEnd (); variter++, i++)
				{
					result[i] = deviceName + "." + (*variter)->getName ();
				}
			}
			for (iter = serv->getConnections ()->begin (); iter != serv->getConnections ()->end (); iter++)
			{
				conn = *iter;
				std::string deviceName = (*iter)->getName ();
				// filter device list
				for (variter = conn->valueBegin (); variter != conn->valueEnd (); variter++, i++)
				{
					result[i] = deviceName + "." + (*variter)->getName ();
				}
			}
		}

		std::string help ()
		{
			return std::string ("Returns name of devices conencted to the system");
		}
} listValues (&xmlrpc_server);

/**
 * List name of all values accessible from server.
 *
 * @author Petr Kubanek <petr@kubanek.net>
 *
 * @addgroup XMLRPC
 */
class ListValuesDevice: public ListValues
{
	public:
		ListValuesDevice (XmlRpcServer* s): ListValues (R2X_DEVICES_VALUES_LIST, s)
		{
		}

		void sessionExecute (XmlRpcValue& params, XmlRpcValue& result)
		{
			XmlRpcd *serv = (XmlRpcd *) getMasterApp ();
			Rts2Conn *conn;
			int i = 0;
			// print results for single device..
			if (params.size() == 1)
			{
				conn = serv->getOpenConnection (((std::string)params[0]).c_str());
				if (!conn)
				{
					throw XmlRpcException ("Cannot get device " + (std::string) params[0]);
				}
				for (Rts2ValueVector::iterator variter = conn->valueBegin (); variter != conn->valueEnd (); variter++, i++)
				{
					XmlRpcValue retVar;
					retVar["name"] = (*variter)->getName ();
					retVar["flags"] = (*variter)->getFlags ();

					Rts2Value *val = *variter;

					switch (val->getValueBaseType ())
					{
						case RTS2_VALUE_INTEGER:
							int int_val;
							int_val = (*variter)->getValueInteger ();
							retVar["value"] = int_val;
							break;
						case RTS2_VALUE_DOUBLE:
							double dbl_value;
							dbl_value = (*variter)->getValueDouble ();
							retVar["value"] = dbl_value;
							break;
						case RTS2_VALUE_FLOAT:
							float float_val;
							float_val = (*variter)->getValueFloat ();
							retVar["value"] = float_val;
							break;
						case RTS2_VALUE_BOOL:
							bool bool_val;
							bool_val = ((Rts2ValueBool*)(*variter))->getValueBool ();
							retVar["value"] = bool_val;
							break;
						case RTS2_VALUE_LONGINT:
							int_val = (*variter)->getValueLong ();
							retVar["value"] = int_val;
							break;
						case RTS2_VALUE_TIME:
							struct tm tm_s;
							long usec;
							((Rts2ValueTime*) (*variter))->getStructTm (&tm_s, &usec);
							retVar["value"] = XmlRpcValue (&tm_s);
							break;
						default:
							retVar["value"] = (*variter)->getValue ();
							break;
					}
					result[i] = retVar;
				}
			}
			// print from all
			else
			{
				connections_t::iterator iter;
				Rts2ValueVector::iterator variter;
				for (iter = serv->getCentraldConns ()->begin (); iter != serv->getCentraldConns ()->end (); iter++)
				{
					conn = *iter;
					std::string deviceName = (*iter)->getName ();
					// filter device list
					int v;
					for (v = 0; v < params.size (); v++)
					{
						if (((std::string) params[v]) == deviceName)
							break;
					}
					if (v == params.size ())
						continue;
					for (variter = conn->valueBegin (); variter != conn->valueEnd (); variter++, i++)
					{
						result[i] = deviceName + "." + (*variter)->getName ();
					}
				}
				for (iter = serv->getConnections ()->begin (); iter != serv->getConnections ()->end (); iter++)
				{
					conn = *iter;
					std::string deviceName = (*iter)->getName ();
					// filter device list
					int v;
					for (v = 0; v < params.size (); v++)
					{
						if (((std::string) params[v]) == deviceName)
							break;
					}
					if (v == params.size ())
						continue;
					for (variter = conn->valueBegin (); variter != conn->valueEnd (); variter++, i++)
					{
						result[i] = deviceName + "." + (*variter)->getName ();
					}
				}
			}
		}

		std::string help ()
		{
			return std::string ("Returns name of devices conencted to the system for given device(s)");
		}
} listValuesDevice (&xmlrpc_server);

class GetValue: public SessionMethod
{
	public:
		GetValue (XmlRpcServer* s) : SessionMethod (R2X_VALUE_GET, s) {}

		void sessionExecute (XmlRpcValue& params, XmlRpcValue& result)
		{
			std::string devName = params[0];
			std::string valueName = params[1];
			XmlRpcd *serv = (XmlRpcd *) getMasterApp ();
			Rts2Conn *conn = serv->getOpenConnection (devName.c_str ());
			if (!conn)
			{
				throw XmlRpcException ("Cannot find connection '" + std::string (devName) + "'.");
			}
			Rts2Value *val = conn->getValue (valueName.c_str ());
			if (!val)
			{
				throw XmlRpcException ("Cannot find value '" + std::string (valueName) + "' on device '" + std::string (devName) + "'.");
			}
			switch (val->getValueBaseType ())
			{
				case RTS2_VALUE_INTEGER:
				case RTS2_VALUE_LONGINT:
					result = val->getValueInteger ();
					break;
				case RTS2_VALUE_FLOAT:
					result = val->getValueFloat ();
					break;
				case RTS2_VALUE_DOUBLE:
					result = val->getValueDouble ();
					break;
				case RTS2_VALUE_BOOL:
					result = ((Rts2ValueBool *)val)->getValueBool ();
					break;
				case RTS2_VALUE_TIME:
					struct tm tm_s;
					long usec;
					((Rts2ValueTime*)val)->getStructTm (&tm_s, &usec);
					result = XmlRpcValue (&tm_s);
					break;
				default:
					result = val->getValue ();
					break;
			}
		}
} getValue (&xmlrpc_server);

class SessionMethodValue:public SessionMethod
{
	protected:
		SessionMethodValue (const char *method, XmlRpcServer *s):SessionMethod (method, s) {}

		void setXmlValutRts2 (Rts2Conn *conn, std::string valueName, XmlRpcValue &x_val)
		{
			int i_val;
			double d_val;
			std::string s_val;

			Rts2Value *val = conn->getValue (valueName.c_str ());
			if (!val)
			{
				throw XmlRpcException ("Cannot find value '" + std::string (valueName) + "' on device '" + std::string (conn->getName ()) + "'.");
			}

			switch (val->getValueBaseType ())
			{
				case RTS2_VALUE_INTEGER:
				case RTS2_VALUE_LONGINT:
					if (x_val.getType () == XmlRpcValue::TypeInt)
					{
						i_val = (int) (x_val);
					}
					else
					{
						s_val = (std::string) (x_val);
						i_val = atoi (s_val.c_str ());
					}
					conn->queCommand (new Rts2CommandChangeValue (conn->getOtherDevClient (), valueName, '=', i_val));
					break;
				case RTS2_VALUE_DOUBLE:
					if (x_val.getType () == XmlRpcValue::TypeDouble)
					{
						d_val = (double) (x_val);
					}
					else
					{
						s_val = (std::string) (x_val);
						d_val = atof (s_val.c_str ());
					}
					conn->queCommand (new Rts2CommandChangeValue (conn->getOtherDevClient (), valueName, '=', d_val));
					break;

				case RTS2_VALUE_FLOAT:
					if (x_val.getType () == XmlRpcValue::TypeDouble)
					{
						d_val = (double) (x_val);
					}
					else
					{
						s_val = (std::string) (x_val);
						d_val = atof (s_val.c_str ());
					}
					conn->queCommand (new Rts2CommandChangeValue (conn->getOtherDevClient (), valueName, '=', (float) d_val));
					break;
				case RTS2_VALUE_STRING:
					conn->queCommand (new Rts2CommandChangeValue (conn->getOtherDevClient (), valueName, '=', (std::string) (x_val)));
					break;
				default:
					conn->queCommand (new Rts2CommandChangeValue (conn->getOtherDevClient (), valueName, '=', (std::string) (x_val), true));
					break;
			}
		}
};

class SetValue: public SessionMethodValue
{
	public:
		SetValue (XmlRpcServer* s) : SessionMethodValue (R2X_VALUE_SET, s) {}

		void sessionExecute (XmlRpcValue& params, XmlRpcValue& result)
		{
			std::string devName = params[0];
			std::string valueName = params[1];
			XmlRpcd *serv = (XmlRpcd *) getMasterApp ();
			Rts2Conn *conn = serv->getOpenConnection (devName.c_str ());
			if (!conn)
			{
				throw XmlRpcException ("Cannot find connection '" + std::string (devName) + "'.");
			}
			setXmlValutRts2 (conn, valueName, params[2]);
		}

		std::string help ()
		{
			return std::string ("Set RTS2 value");
		}

} setValue (&xmlrpc_server);

class SetValueByType: public SessionMethodValue
{
	public:
		SetValueByType (XmlRpcServer* s) : SessionMethodValue (R2X_VALUE_BY_TYPE_SET, s) {}

		void sessionExecute (XmlRpcValue& params, XmlRpcValue& result)
		{
			int devType = params[0];
			std::string valueName = params[1];
			XmlRpcd *serv = (XmlRpcd *) getMasterApp ();
			connections_t::iterator iter = serv->getConnections ()->begin ();
			serv->getOpenConnectionType (devType, iter);
			if (iter == serv->getConnections ()->end ())
			{
				throw XmlRpcException ("Cannot find connection of given type");
			}
			for (; iter != serv->getConnections ()->end (); serv->getOpenConnectionType (devType, ++iter))
			{
				setXmlValutRts2 (*iter, valueName, params[2]);
			}
		}

		std::string help ()
		{
			return std::string ("Set RTS2 value for device by type");
		}

} setValueByType (&xmlrpc_server);

class IncValue: public SessionMethod
{
	public:
		IncValue (XmlRpcServer* s) : SessionMethod (R2X_VALUE_INC, s) {}

		void sessionExecute (XmlRpcValue& params, XmlRpcValue& result)
		{
			std::string devName = params[0];
			std::string valueName = params[1];
			XmlRpcd *serv = (XmlRpcd *) getMasterApp ();
			Rts2Conn *conn = serv->getOpenConnection (devName.c_str ());
			if (!conn)
			{
				throw XmlRpcException ("Cannot find connection '" + std::string (devName) + "'.");
			}
			Rts2Value *val = conn->getValue (valueName.c_str ());
			if (!val)
			{
				throw XmlRpcException ("Cannot find value '" + std::string (valueName) + "' on device '" + std::string (devName) + "'.");
			}

			int i_val;
			double d_val;
			std::string s_val;
			XmlRpcValue x_val = params[2];
			switch (val->getValueBaseType ())
			{
				case RTS2_VALUE_INTEGER:
				case RTS2_VALUE_LONGINT:
					if (x_val.getType () == XmlRpcValue::TypeInt)
					{
						i_val = (int) (x_val);
					}
					else
					{
						s_val = (std::string) (x_val);
						i_val = atoi (s_val.c_str ());
					}
					conn->queCommand (new Rts2CommandChangeValue (conn->getOtherDevClient (), valueName, '+', i_val));
					break;
				case RTS2_VALUE_DOUBLE:
					if (x_val.getType () == XmlRpcValue::TypeDouble)
					{
						d_val = (double) (x_val);
					}
					else
					{
						s_val = (std::string) (x_val);
						d_val = atof (s_val.c_str ());
					}
					conn->queCommand (new Rts2CommandChangeValue (conn->getOtherDevClient (), valueName, '+', d_val));
					break;

				case RTS2_VALUE_FLOAT:
					if (x_val.getType () == XmlRpcValue::TypeDouble)
					{
						d_val = (double) (x_val);
					}
					else
					{
						s_val = (std::string) (x_val);
						d_val = atof (s_val.c_str ());
					}
					conn->queCommand (new Rts2CommandChangeValue (conn->getOtherDevClient (), valueName, '+', (float) d_val));
					break;
				case RTS2_VALUE_STRING:
					conn->queCommand (new Rts2CommandChangeValue (conn->getOtherDevClient (), valueName, '+', (std::string) (params[2])));
					break;
				default:
					conn->queCommand (new Rts2CommandChangeValue (conn->getOtherDevClient (), valueName, '+', (std::string) (params[2]), true));
					break;
			}
		}

		std::string help ()
		{
			return std::string ("Increment RTS2 value");
		}

} incValue (&xmlrpc_server);


class GetMessages: public SessionMethod
{
	public:
		GetMessages (XmlRpcServer* s) : SessionMethod (R2X_MESSAGES_GET, s) {}

		void sessionExecute (XmlRpcValue& params, XmlRpcValue& result)
		{
			int i = 0;
			XmlRpcValue retVar;
			for (std::deque <Rts2Message>::iterator iter = ((XmlRpcd *) getMasterApp ())->getMessages ().begin ();
				iter != ((XmlRpcd *) getMasterApp ())->getMessages ().end (); iter++, i++)
			{
				XmlRpcValue val;
				Rts2Message msg = *iter;
				time_t t;
				val["type"] = msg.getTypeString ();
				val["origin"] = msg.getMessageOName ();
				t = msg.getMessageTime ();
				val["timestamp"] = XmlRpcValue (gmtime (&t));
				val["message"] = msg.getMessageString ();
				result[i] = val;
			}

		}
} getMessages (&xmlrpc_server);


#ifdef HAVE_PGSQL
/*
 *
 */
class ListTargets: public SessionMethod
{
	public:
		ListTargets (XmlRpcServer* s) : SessionMethod (R2X_TARGETS_LIST, s) {}

		void sessionExecute (XmlRpcValue& params, XmlRpcValue& result)
		{
			rts2db::TargetSet *tar_set = new rts2db::TargetSet ();
			tar_set->load ();

			double value;
			int i = 0;
			XmlRpcValue retVar;

			for (rts2db::TargetSet::iterator tar_iter = tar_set->begin(); tar_iter != tar_set->end (); tar_iter++, i++)
			{
				Target *tar = (*tar_iter).second;
				retVar["id"] = tar->getTargetID ();
				retVar["type"] = tar->getTargetType ();
				if (tar->getTargetName ())
					retVar["name"] = tar->getTargetName ();
				else
					retVar["name"] = "NULL";

				retVar["enabled"] = tar->getTargetEnabled ();
				if (tar->getTargetComment ())
					retVar["comment"] = tar->getTargetComment ();
				else
					retVar["comment"] = "";
				value = tar->getLastObs();
				retVar["last_obs"] = value;
				struct ln_equ_posn pos;
				tar->getPosition (&pos, ln_get_julian_from_sys ());
				retVar["ra"] = pos.ra;
				retVar["dec"] = pos.dec;
				result[i++] = retVar;
			}
		}

		std::string help ()
		{
			return std::string ("Returns all targets");
		}

} listTargets (&xmlrpc_server);


class ListTargetsByType: public SessionMethod
{
	public:
		ListTargetsByType (XmlRpcServer* s) : SessionMethod (R2X_TARGETS_TYPE_LIST, s) {}

		void sessionExecute (XmlRpcValue& params, XmlRpcValue& result)
		{
			char target_types[params.size ()+1];
			int j;
			for (j = 0; j < params.size (); j++)
				target_types[j] = *(((std::string)params[j]).c_str());
			target_types[j] = '\0';

			rts2db::TargetSet *tar_set = new rts2db::TargetSet (target_types);
			tar_set->load ();

			double value;
			int i = 0;
			XmlRpcValue retVar;

			double JD = ln_get_julian_from_sys ();

			for (rts2db::TargetSet::iterator tar_iter = tar_set->begin(); tar_iter != tar_set->end (); tar_iter++, i++)
			{
				Target *tar = (*tar_iter).second;
				retVar["id"] = tar->getTargetID ();
				retVar["type"] = tar->getTargetType ();
				if (tar->getTargetName ())
					retVar["name"] = tar->getTargetName ();
				else
					retVar["name"] = "NULL";

				retVar["enabled"] = tar->getTargetEnabled ();
				if (tar->getTargetComment ())
					retVar["comment"] = tar->getTargetComment ();
				else
					retVar["comment"] = "";
				value = tar->getLastObs();
				retVar["last_obs"] = value;
				struct ln_equ_posn pos;
				tar->getPosition (&pos, JD);
				retVar["ra"] = pos.ra;
				retVar["dec"] = pos.dec;
				struct ln_hrz_posn hrz;
				tar->getAltAz (&hrz, JD);
				retVar["alt"] = hrz.alt;
				retVar["az"] = hrz.az;
				result[i++] = retVar;
			}
		}

		std::string help ()
		{
			return std::string ("Returns all targets");
		}

} listTargetsByType (&xmlrpc_server);


class TargetInfo: public SessionMethod
{
	public:
		TargetInfo (XmlRpcServer* s) : SessionMethod (R2X_TARGETS_INFO, s) {}

		void sessionExecute (XmlRpcValue& params, XmlRpcValue& result)
		{
			std::list < int >targets;
			XmlRpcValue retVar;
			int i;
			double JD;

			for (i = 0; i < params.size(); i++)
				targets.push_back (params[i]);

			rts2db::TargetSet *tar_set = new rts2db::TargetSet ();
			tar_set->load (targets);

			i = 0;

			for (rts2db::TargetSet::iterator tar_iter = tar_set->begin(); tar_iter != tar_set->end (); tar_iter++)
			{
				JD = ln_get_julian_from_sys ();

				Target *tar = (*tar_iter).second;

				XmlStream xs (&retVar);
				xs << *tar;

				// observations
				rts2db::ObservationSet *obs_set;
				obs_set = new rts2db::ObservationSet (tar->getTargetID ());
				int j = 0;
				for (rts2db::ObservationSet::iterator obs_iter = obs_set->begin(); obs_iter != obs_set->end(); obs_iter++, j++)
				{
					retVar["observation"][j]["obsid"] =  (*obs_iter).getObsId();
					retVar["observation"][j]["images"] = (*obs_iter).getNumberOfImages();
				}

				retVar["HOUR"] = tar->getHourAngle (JD);
				tar->sendInfo (xs, JD);
				result[i++] = retVar;
			}
		}

} targetInfo (&xmlrpc_server);

class TargetAltitude: public SessionMethod
{
	public:
		TargetAltitude (XmlRpcServer *s) : SessionMethod (R2X_TARGET_ALTITUDE, s) {}

		void sessionExecute (XmlRpcValue& params, XmlRpcValue& result)
		{
			if (params.size () != 4)
			{
				throw XmlRpcException ("Invalid number of parameters");
			}
			Target *tar = createTarget ((int) params[0], Rts2Config::instance()->getObserver ());
			if (tar == NULL)
			{
				throw XmlRpcException ("Cannot create target");
			}
			time_t tfrom = (int)params[1];
			double jd_from = ln_get_julian_from_timet (&tfrom);
			time_t tto = (int)params[2];
			double jd_to = ln_get_julian_from_timet (&tto);
			double stepsize = ((double)params[3]) / 86400;
			// test for insane request
			if ((jd_to - jd_from) / stepsize > 10000)
			{
				throw XmlRpcException ("Too many points");
			}
			int i;
			double j;
			for (i = 0, j = jd_from; j < jd_to; i++, j += stepsize)
			{
				result[i][0] = j;
				ln_hrz_posn hrz;
				tar->getAltAz (&hrz, j);
				result[i][1] = hrz.alt;
			}
		}

		std::string help ()
		{
			return std::string ("Return 2D array with informations about target height at diferent times.");
		}
} targetAltitude (&xmlrpc_server);

class ListTargetObservations: public SessionMethod
{
	public:
		ListTargetObservations (XmlRpcServer* s) : SessionMethod (R2X_TARGET_OBSERVATIONS_LIST, s) {}

		void sessionExecute (XmlRpcValue& params, XmlRpcValue& result)
		{
			XmlRpcValue retVar;
			rts2db::ObservationSet *obs_set;
			obs_set = new rts2db::ObservationSet ((int)params[0]);
			int i = 0;
			time_t t;
			for (rts2db::ObservationSet::iterator obs_iter = obs_set->begin(); obs_iter != obs_set->end(); obs_iter++, i++)
			{
				retVar["id"] = (*obs_iter).getObsId();
				retVar["obs_ra"] = (*obs_iter).getObsRa();
				retVar["obs_dec"] = (*obs_iter).getObsDec();
				t = (*obs_iter).getObsStart();
				retVar["obs_start"] = XmlRpcValue (gmtime (&t));
				t = (*obs_iter).getObsEnd();
				retVar["obs_end"] = XmlRpcValue (gmtime(&t));
				retVar["obs_images"] = (*obs_iter).getNumberOfImages();

				result[i] = retVar;
			}
		}

		std::string help ()
		{
			return std::string ("returns observations of given target");
		}

} listTargetObservations (&xmlrpc_server);

class ListMonthObservations: public SessionMethod
{
	public:
		ListMonthObservations (XmlRpcServer* s) : SessionMethod (R2X_OBSERVATIONS_MONTH, s) {}

		void sessionExecute (XmlRpcValue& params, XmlRpcValue& result)
		{
			XmlRpcValue retVar;
			rts2db::ObservationSet *obs_set;
			obs_set = new rts2db::ObservationSet ((int)params[0], (int)params[1]);
			int i = 0;
			time_t t;
			for (rts2db::ObservationSet::iterator obs_iter = obs_set->begin(); obs_iter != obs_set->end(); obs_iter++, i++)
			{
				retVar["id"] = (*obs_iter).getObsId();
				retVar["tar_id"] = (*obs_iter).getTargetId ();
				retVar["tar_name"] = (*obs_iter).getTargetName ();
				retVar["obs_ra"] = (*obs_iter).getObsRa();
				retVar["obs_dec"] = (*obs_iter).getObsDec();
				t = (time_t) ((*obs_iter).getObsStart());
				if (t < 0)
				  t = 0;
				retVar["obs_start"] = XmlRpcValue (gmtime (&t));
				t = (time_t) ((*obs_iter).getObsEnd());
				if (t < 0)
				  t = 0;
				retVar["obs_end"] = XmlRpcValue (gmtime (&t));
				retVar["obs_images"] = (*obs_iter).getNumberOfImages();

				result[i] = retVar;
			}
		}

} listMonthObservations (&xmlrpc_server);

class ListImages: public SessionMethod
{
	public:
		ListImages (XmlRpcServer* s) : SessionMethod (R2X_OBSERVATION_IMAGES_LIST, s) {}

		void sessionExecute (XmlRpcValue& params, XmlRpcValue& result)
		{
			XmlRpcValue retVar;
			Rts2Obs *obs;
			obs = new Rts2Obs ((int)params[0]);
			if (obs->loadImages ())
				return;
			Rts2ImgSet *img_set = obs->getImageSet();
			int i = 0;
			for (Rts2ImgSet::iterator img_iter = img_set->begin(); img_iter != img_set->end(); img_iter++)
			{
				double eRa, eDec, eRad;
				eRa = eDec = eRad = nan ("f");
				Rts2Image *image = *img_iter;
				retVar["filename"] = image->getFileName ();
				retVar["start"] = image->getExposureStart ();
				retVar["exposure"] = image->getExposureLength ();
				retVar["filter"] = image->getFilter ();
				image->getError (eRa, eDec, eRad);
				retVar["error_ra"] = eRa;
				retVar["error_dec"] = eDec;
				retVar["error_pos"] = eRad;
				result[i++] = retVar;
			}
		}
} listImages (&xmlrpc_server);


class TicketInfo: public SessionMethod
{
	public:
		TicketInfo (XmlRpcServer *s): SessionMethod (R2X_TICKET_INFO, s) {}

		void sessionExecute (XmlRpcValue& params, XmlRpcValue& result)
		{
			if (params.size () != 1)
			{
				throw XmlRpcException ("Invalid number of parameters");
			}

			try
			{
				rts2sched::Ticket t = rts2sched::Ticket (params[0]);
				t.load ();
				XmlStream xs (&result);

				xs << t;
			}
			catch (rts2db::SqlError e)
			{
				throw XmlRpcException (e.what ());
			}
		}
} ticketInfo (&xmlrpc_server);


class RecordsValues: public SessionMethod
{
	public:
		RecordsValues (XmlRpcServer* s): SessionMethod (R2X_RECORDS_VALUES, s) {}
		void sessionExecute (XmlRpcValue& params, XmlRpcValue& result)
		{
//			if (params.getType () != XmlRpcValue::TypeInvalid)
//				throw XmlRpcException ("Invalid number of parameters");
			try
			{
				rts2db::RecvalsSet recvals = rts2db::RecvalsSet ();
				int i = 0;
				time_t t;
				recvals.load ();
				for (rts2db::RecvalsSet::iterator iter = recvals.begin (); iter != recvals.end (); iter++)
				{
					rts2db::Recval rv = (*iter);
					XmlRpcValue res;
					res["id"] = rv.getId ();
					res["device"] = rv.getDevice ();
					res["value_name"] = rv.getValueName ();
					res["value_type"] = rv.getType ();
					t = rv.getFrom ();
					res["from"] = XmlRpcValue (gmtime (&t));
					t = rv.getTo ();
					res["to"] = XmlRpcValue (gmtime (&t));
					result[i++] = res;
				}
			}
			catch (rts2db::SqlError err)
			{
				throw XmlRpcException (err.what ());
			}
		}
} recordValues (&xmlrpc_server);


class Records: public SessionMethod
{
	public:
		Records (XmlRpcServer* s): SessionMethod (R2X_RECORDS_GET, s) {}
		void sessionExecute (XmlRpcValue& params, XmlRpcValue& result)
		{
			if (params.size () != 3)
				throw XmlRpcException ("Invalid number of parameters");

			try
			{
				rts2db::RecordsSet recset = rts2db::RecordsSet (params[0]);
				int i = 0;
				time_t t;
				recset.load (params[1], params[2]);
				for (rts2db::RecordsSet::iterator iter = recset.begin (); iter != recset.end (); iter++)
				{
					rts2db::Record rv = (*iter);
					XmlRpcValue res;
					t = rv.getRecTime ();
					res[0] = XmlRpcValue (gmtime (&t));
					res[1] = rv.getValue ();
					result[i++] = res;
				}
			}
			catch (rts2db::SqlError err)
			{
				throw XmlRpcException (err.what ());
			}
		}
} records (&xmlrpc_server);


class RecordsAverage: public SessionMethod
{
	public:
		RecordsAverage (XmlRpcServer* s): SessionMethod (R2X_RECORDS_AVERAGES, s) {}
		void sessionExecute (XmlRpcValue& params, XmlRpcValue& result)
		{
			if (params.size () != 3)
				throw XmlRpcException ("Invalid number of parameters");

			try
			{
				rts2db::RecordAvgSet recset = rts2db::RecordAvgSet (params[0], rts2db::HOUR);
				int i = 0;
				time_t t;
				recset.load (params[1], params[2]);
				for (rts2db::RecordAvgSet::iterator iter = recset.begin (); iter != recset.end (); iter++)
				{
					rts2db::RecordAvg rv = (*iter);
					XmlRpcValue res;
					t = rv.getRecTime ();
					res[0] = XmlRpcValue (gmtime (&t));
					res[1] = rv.getAverage ();
					res[2] = rv.getMinimum ();
					res[3] = rv.getMaximum ();
					res[4] = rv.getRecCout ();
					result[i++] = res;
				}
			}
			catch (rts2db::SqlError err)
			{
				throw XmlRpcException (err.what ());
			}
		}
} recordAverage (&xmlrpc_server);

#ifdef HAVE_LIBJPEG

Graph graph ("/graph", &xmlrpc_server);

AltAzTarget altAzTarget ("/altaz", &xmlrpc_server);

CurrentPosition current ("/current", &xmlrpc_server);

#endif /* HAVE_LIBJPEG */

Auger auger ("/auger", &xmlrpc_server);

Targets targets ("/targets", &xmlrpc_server);

AddTarget addTarget ("/addtarget", &xmlrpc_server);

class UserLogin: public XmlRpcServerMethod
{
	public:
		UserLogin (XmlRpcServer* s) : XmlRpcServerMethod (R2X_USER_LOGIN, s) {}

		void execute (XmlRpcValue& params, XmlRpcValue& result)
		{
			if (params.size() != 2)
				throw XmlRpcException ("Invalid number of parameters");

			result = verifyUser (params[0], params[1]);
		}
} userLogin (&xmlrpc_server);

#endif /* HAVE_PGSQL */

int main (int argc, char **argv)
{
	XmlRpcd device = XmlRpcd (argc, argv);
	return device.run ();
}
