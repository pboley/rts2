/*! 
 * Standart image header. Used for transforming image header data between
 * camera deamon and camera client.
 *
 * $Id$
 *
 * It's quite necessary to have such a head, since condition on camera could
 * change unpredicitably after readout command was issuedddand client could
 * therefore receiver misleading information.
 *
 * That header is in no way mean as universal header for astronomical images.
 * It's only internal used among componnents of the rts software. It should
 * NOT be stored in permanent form, since it could change with versions of the
 * software.
 *
 * @author petr
 */

#ifndef __RTS_IMGHDR__
#define __RTS_IMGHDR__

#include <time.h>

#define STATUS_FLIP	0x01
#define	STATUS_DARK	0x02

#define MAX_AXES	5	//! Maximum number of axes we should considered.

struct imghdr
{
  int data_type;
  int naxes;			//! Number of axess.
  long sizes[MAX_AXES];		//! Sizes in given axes.
  int binnings[MAX_AXES];	//! Binning in each axe - eg. 2 -> 1 image pixel on given axis is equal 2 ccd pixels.
  char filter;			//! Camera filter
  int status;			//! image status
};

#endif // __RTS_IMGHDR__
