/* 
 * Obs. Vermes cupola driver.
 * Copyright (C) 2010 Markus Wildi <markus.wildi@one-arcsec.org>
 * based on Petr Kubanek's dummy_cup.cpp
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

#include "cupola.h"
#include "../utils/rts2config.h" 
#include "vermes.h" 

#ifdef __cplusplus
extern "C"
{
#endif
// wildi ToDo: go to dome-target-az.h
double dome_target_az( struct ln_equ_posn tel_eq, int angle, struct ln_lnlat_posn *obs) ;
#ifdef __cplusplus
}
#endif

#include "barcodereader_vermes.h"
int barcodereader_state ;
double barcodereader_az ;
double barcodereader_dome_azimut_offset= -253.6 ; // wildi ToDo: make an option
#ifdef __cplusplus
extern "C"
{
#endif
int set_setpoint( float setpoint, int direction) ;
float get_setpoint() ;
void motor_run_switch_state() ;
void connectDevice( int power_state) ;
#ifdef __cplusplus
}
#endif



using namespace rts2dome;

namespace rts2dome
{
/**
 * Obs. Vermes cupola driver.
 *
 * @author Markus Wildi <markus.wildi@one-arcsec.org>
 */
  class Vermes:public Cupola
  {
  private:
    struct ln_lnlat_posn *obs ;
    struct ln_equ_posn tel_eq ;
    Rts2Config *config ;
    Rts2ValueInteger *barcode_reader_state ;
    Rts2ValueDouble  *azimut_difference ;
    Rts2ValueString  *ssd650v_state ;
    Rts2ValueBool    *ssd650v_on_off ;
    Rts2ValueDouble  *ssd650v_set_point ;

    void parkCupola ();
  protected:
    virtual int moveStart () ;
    virtual int moveEnd () ;
    virtual long isMoving () ;
    // there is no dome door to open 
    virtual int startOpen (){return 0;}
    virtual long isOpened (){return -2;}
    virtual int endOpen (){return 0;}
    virtual int startClose (){return 0;}
    virtual long isClosed (){return -2;}
    virtual int endClose (){return 0;}

  public:
    Vermes (int argc, char **argv) ;
    virtual int initValues () ;
    virtual double getSplitWidth (double alt) ;
    virtual int info () ;
    virtual int idle ();
    virtual void valueChanged (Rts2Value * changed_value) ;
    // park copula
    virtual int standby ();
    virtual int off ();
  };
}

int Vermes::moveEnd ()
{
  //	logStream (MESSAGE_ERROR) << "Vermes::moveEnd set Az "<< hrz.az << sendLog ;
  logStream (MESSAGE_ERROR) << "Vermes::moveEnd did nothing "<< sendLog ;
  return Cupola::moveEnd ();
}
long Vermes::isMoving ()
{
  logStream (MESSAGE_DEBUG) << "Vermes::isMoving"<< sendLog ;



  if ( 1) // if there, return -2
    return -2;
  return USEC_SEC;
}
int Vermes::moveStart ()
{
  tel_eq.ra= getTargetRa() ;
  tel_eq.dec= getTargetDec() ;

  logStream (MESSAGE_ERROR) << "Vermes::moveStart RA " << tel_eq.ra  << " Dec " << tel_eq.dec << sendLog ;

  double target_az= -1. ;
  target_az= dome_target_az( tel_eq, -1,  obs) ; // wildi ToDo: DecAxis!
  
  logStream (MESSAGE_ERROR) << "Vermes::moveStart dome target az" << target_az << sendLog ;
  setTargetAz(target_az) ;
  return Cupola::moveStart ();
}

double Vermes::getSplitWidth (double alt)
{
  logStream (MESSAGE_ERROR) << "Vermes::getSplitWidth returning 1" << sendLog ;
  return 1;
}

void Vermes::parkCupola ()
{
  logStream (MESSAGE_ERROR) << "Vermes::parkCupola doing nothing" << sendLog ;
}

int Vermes::standby ()
{
  logStream (MESSAGE_ERROR) << "Vermes::standby doing nothing" << sendLog ;
  parkCupola ();
  return Cupola::standby ();
}

int Vermes::off ()
{
  connectDevice(SSD650V_DISCONNECT) ;

  logStream (MESSAGE_ERROR) << "Vermes::off disconnecting from frequency inverter" << sendLog ;
  parkCupola ();
  return Cupola::off ();
}

void Vermes::valueChanged (Rts2Value * changed_value)
{
  if (changed_value == ssd650v_on_off)
    {
      
    }
  else if (changed_value == ssd650v_set_point)
    {
      float setpoint= (float) ssd650v_set_point->getValueDouble() ;

      if( abs( setpoint) < 100. )
	{
	  int direction= 1 ;
	  if( setpoint < 0.)
	    {
	      direction= -1 ;
	      setpoint *= -1 ;
	    }
	  else if(setpoint== 0)
	    {
	      direction= 0 ;
	    }
	  if( set_setpoint( setpoint, direction)) 
	  {
	    logStream (MESSAGE_ERROR) << "Vermes::valueChanged could not set setpoint "<< setpoint << " direction"<< direction<< sendLog ;
	  }
	  return ; // ask Petr what to do in general if something fails within ::valueChanged
	}
      else
	{
	  ssd650v_set_point->setValueDouble(-1) ; // ask Petr what to do in general if something fails within ::valueChanged
	  return ;
	}
    }
  Cupola::valueChanged (changed_value);

}
int Vermes::idle ()
{
	return Cupola::idle ();
}
int Vermes::info ()
{
  barcode_reader_state->setValueInteger( barcodereader_state) ; 
  setCurrentAz (barcodereader_az);

  azimut_difference->setValueDouble( ( getTargetAz()-barcodereader_az)) ;
  ssd650v_state->setValueString("running FAKE") ;
  ssd650v_on_off->setValueBool( 0) ;


  
  ssd650v_set_point->setValueDouble( (double) get_setpoint()) ;

  // not Cupola::info() ?!?
  return Cupola::info ();
}
int Vermes::initValues ()
{
  int ret ;
  config = Rts2Config::instance ();

  ret = config->loadFile ();
  if (ret)
    return -1;


  if(!( ret= start_bcr_comm()))
    {
      register_pos_change_callback(position_update_callback);
    }
  else
    {
      logStream (MESSAGE_ERROR) << "Vermes::initValues could connect to barcode devices, exiting "<< sendLog ;
      exit(1) ;
    }

  connectDevice(SSD650V_CONNECT) ;

  obs= Cupola::getObserver() ;

  return Cupola::initValues ();
}
Vermes::Vermes (int in_argc, char **in_argv):Cupola (in_argc, in_argv) 
{
  // since this driver is Obs. Vermes specific no options are really required
  createValue (azimut_difference,   "AZdiff",     "target - actual azimuth reading", false, RTS2_DT_DEGREES  );
  createValue (barcode_reader_state,"BCRstate",   "state of the barcodereader value CUP_AZ (0=valid, 1=invalid)", false);
  createValue (ssd650v_state,       "SSDstate",   "status of the ssd650v inverter ", false);
  createValue (ssd650v_on_off,      "SSDswitch",  "(true=running, false=not running)", false, RTS2_VALUE_WRITABLE);
  createValue (ssd650v_set_point,   "SSDsetpoint","ssd650v setpoint", false, RTS2_VALUE_WRITABLE);

  barcode_reader_state->setValueInteger( -1) ; 
}
int main (int argc, char **argv)
{
	Vermes device (argc, argv);
	return device.run ();
}
