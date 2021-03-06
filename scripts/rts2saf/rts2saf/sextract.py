#!/usr/bin/python
# (C) 2013, Markus Wildi, markus.wildi@bluewin.ch
#
#   This program is free software; you can redistribute it and/or modify
#   it under the terms of the GNU General Public License as published by
#   the Free Software Foundation; either version 2, or (at your option)
#   any later version.
#
#   This program is distributed in the hope that it will be useful,
#   but WITHOUT ANY WARRANTY; without even the implied warranty of
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#   GNU General Public License for more details.
#
#   You should have received a copy of the GNU General Public License
#   along with this program; if not, write to the Free Software Foundation,
#   Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
#
#   Or visit http://www.gnu.org/licenses/gpl.html.
#
"""
"""

__author__ = 'markus.wildi@bluewin.ch'

import pyfits
import math
# ToDo sort that out with Petr import rts2.sextractor as rsx
import rts2saf.sextractor as rsx
from rts2saf.data import DataSxtr

class Sextract(object):
    """Sextract a FITS image using :py:mod:`rts2.sextractor`.

    :var debug: enable more debug output with --debug and --level
    :var createAssoc: if True create a SExtractor catalog for association (ASSOC)
    :var rt: run time configuration,  :py:mod:`rts2saf.config.Configuration`, usually read from /usr/local/etc/rts2/rts2saf/rts2saf.cfg
    :var logger:  :py:mod:`rts2saf.log`

    """
    def __init__(self, debug=False, createAssoc=False, rt=None, logger=None):
        self.debug=debug
        self.createAssoc=createAssoc
        self.rt= rt
        self.sexpath=rt.cfg['SEXPATH']
        self.sexconfig=rt.cfg['SEXCFG']
        self.starnnw=rt.cfg['STARNNW_NAME']
        self.fields=rt.cfg['FIELDS'][:] # it is a reference, gell :-))
        self.nbrsFtwsInuse=len(rt.cfg['FILTER WHEELS INUSE'])
        self.stdFocPos=rt.cfg['FOCUSER_RESOLUTION']
        self.focPosInterval = rt.cfg['FOCUSER_INTERVAL'] 
        self.logger=logger
        
    def appendFluxFields(self):
        """Append SExtractor's parameters to enable flux analysis. 
        """
        self.fields.append('FLUX_MAX')
        self.fields.append('FLUX_APER')
        self.fields.append('FLUXERR_APER')

    def appendAssocFields(self):
        """Append SExtractor's parameters to enable association.
        """
        self.fields.append('VECTOR_ASSOC({0:d})'.format(len(self.fields)))
        self.fields.append('NUMBER_ASSOC')

    def sextract(self, fitsFn=None, assocFn=None):
        """Call :py:mod:`rts2.sextractor`, retrieve FITS header information and create :py:mod:`rts2saf.data.DataSxtr`
        :param fitsFn: FITS file name
        :param assocFn: File name of the association catalog

        :return:  :py:mod:`rts2saf.data.DataSxtr`

        """

        try:
            hdr = pyfits.open(fitsFn,'readonly')[0].header
        except Exception, e:
            self.logger.error( 'sextract: returning, FITS {0}'.format(fitsFn))
            self.logger.error( 'sextract: message rts2saf.sextractor: {0}'.format(e))
            return None

        try:
            focPos = float(hdr['FOC_POS'])
        except Exception, e:
            self.logger.error( 'sextract: in FITS {0}, returning key word FOC_POS not found'.format(fitsFn))
            self.logger.error( 'sextract: message rts2saf.sextractor: {0}'.format(e))
            return None


        if len(self.focPosInterval):
            if min(self.focPosInterval) < focPos < max(self.focPosInterval):
                pass
            else:
                self.logger.info( 'sextract: exclude FOC_POS: {0:5d}, {1}'.format(int(focPos), fitsFn))
                return None
                

        # these values are remapped in config.py
        try:
            # real header key word is mapped in rts2saf.config
            ambientTemp = '{0:3.1f}'.format(float(hdr[self.rt.cfg['AMBIENTTEMPERATURE']]))
        except Exception, e:
            # that is not required
            ambientTemp='NoTemp'
            if self.debug: self.logger.debug( 'sextract: temperature information found: {0}, error: {1}'.format(fitsFn, e))

        try:
            # real header key word is mapped in rts2saf.config, this one is mapped twice!
            # here, this is a 2 dim dict
            binning = float(self.rt.cfg['FITS_BINNING_MAPPING'][hdr[self.rt.cfg['BINNING']]])
            if self.debug: self.logger.debug( 'sextract: binning: {0}'.format(binning))
        except Exception, e:
            self.logger.warn( 'sextract: in FITS {0},  no binning information found: {0}'.format(fitsFn, e))
            # if CatalogAnalysis is done
            binning=None

        binningXY=None
        if binning is None:
            binningXY=list()
            try:
                # real header key word is mapped in rts2saf.config
                binningXY.append(float(hdr[self.rt.cfg['BINNING_X']]))
                if self.debug: self.logger.debug( 'sextract: binningX: {0}'.format(float(hdr[self.rt.cfg['BINNING_X']])))
            except Exception, e:
                # if CatalogAnalysis is done
                self.logger.warn( 'sextract: no x-binning information found:{0}, error: {1}'.format(fitsFn, e))

            try:
                # real header key word is mapped in rts2saf.config
                binningXY.append(float(hdr[self.rt.cfg['BINNING_Y']]))
                if self.debug: self.logger.debug( 'sextract: binningY: {0}'.format(float(hdr[self.rt.cfg['BINNING_Y']])))
            except Exception, e:
                # if CatalogAnalysis is done
                self.logger.warn( 'sextract: no y-binning information found: {0}, error: {1}'.format(fitsFn, e))

            if len(binningXY) == 2 and  binningXY[0] ==  binningXY[1]:
                binning =  binningXY[0]                    
            else:
                self.logger.warn( 'sextract: no valid binning information found: {0}, {1}'.format(repr(binningXY), fitsFn))
                binning = 1 
                self.logger.info( 'sextract: setting binning to: {0}'.format(1))

        try:
            naxis1 = float(hdr['NAXIS1'])
        except Exception, e:
            self.logger.warn( 'sextract: no NAXIS1 information found: {0}, focPos: {1}, error: {2}'.format(fitsFn, focPos, e))
            naxis1=None
        try:
            naxis2 = float(hdr['NAXIS2'])
        except Exception, e:
            self.logger.warn( 'sextract: no NAXIS2 information found: {0}, focPos: {1}, errot: {2}'.format(fitsFn, focPos, e))
            naxis2=None
        try:
            ftName = hdr['FILTER']
        except Exception, e:
            self.logger.warn( 'sextract: no filter name information found: {0}, focPos: {1}, error:{2}'.format(fitsFn, focPos, e))
            ftName=None

        try:
            date = hdr['DATE'] # DATE-OBS
        except Exception, e:
            self.logger.warn( 'sextract: no date information found: {0}, focPos: {1}, error: {2}'.format(fitsFn, focPos, e))
            date=None


        # ToDo clumsy
        ftAName=None
        if self.nbrsFtwsInuse > 0:
            try:
                ftAName = hdr['FILTA']
            except Exception, e:
                if self.debug: self.logger.debug( 'sextract: no FILTA name information found: {0}, focPos: {1}, error: {2}'.format(fitsFn, focPos, e))

        # ToDo clumsy
        ftBName=None
        if self.nbrsFtwsInuse > 1:
            try:
                ftBName = hdr['FILTB']
            except Exception, e:
                if self.debug: self.logger.debug( 'sextract: no FILTB name information found: {0}, error: {1}'.format(fitsFn, focPos, e))

        # ToDo clumsy
        ftCName=None
        if self.nbrsFtwsInuse > 2:
            try:
                ftCName = hdr['FILTC']
            except Exception, e:
                if self.debug: self.logger.debug( 'sextract: no FILTC name information found: {0}, error: {1}'.format(fitsFn, focPos, e))


        sex = rsx.Sextractor(fields=self.fields,sexpath=self.sexpath,sexconfig=self.sexconfig,starnnw=self.starnnw, createAssoc=self.createAssoc)
        # create the skyList
        stde= sex.runSExtractor(fitsFn, assocFn=assocFn)

        if stde:
            self.logger.error( 'sextract: returning, message rts2saf.sextractor: {0}'.format(stde))
            #return None
        
        # find the sextractor counts
        objectCount = len(sex.cleanedObjects)


        try:
            fwhm,stdFwhm,nstars=sex.calculate_FWHM(filterGalaxies=False)
#            self.logger.warn( '++++++++++++++++++++ {0} {1:5.2f}, {2:5.2f}, {3} {4}'.format(focPos, fwhm, stdFwhm, len(sex.cleanedObjects), len(sex.cleanedObjects[0])))
#            self.logger.warn( '++++++++++++++++++++ {0} {1}, '.format(focPos, sex.cleanedObjects))
        except Exception, e:
            self.logger.warn( 'sextract: focPos: {0:5.0f}, raw objects: {1}, no objects found, {0} (after filtering), {2}'.format(focPos, objectCount, fitsFn, ))
            self.logger.warn( 'sextract: message rts2saf.sextractor: {0}'.format(e))
            return None

        if math.isnan(fwhm):
            self.logger.warn( 'sextract: focPos: {0:5.0f}, raw objects: {1}, fwhm is NaN, rts2saf.sextractor/numpy failed on {2}'.format(focPos, objectCount, fitsFn))
            return None

        # take care of binning
        fwhm = float(fwhm) *  binning
        stdFwhm = float(stdFwhm) *  binning
        
        i_fwhm = self.fields.index('FWHM_IMAGE')
        for sxo in sex.objects:
            sxo[i_fwhm] *= binning
        

        # store results
        dataSxtr=DataSxtr(
            date=date,
            fitsFn=fitsFn, 
            focPos=focPos, 
            stdFocPos=float(self.stdFocPos), # nothing real...
            fwhm=fwhm, 
            stdFwhm=stdFwhm,
            nstars=int(nstars), 
            ambientTemp=ambientTemp, 
            rawCatalog=sex.objects, 
            catalog=sex.cleanedObjects, 
            binning=binning, 
            binningXY=binningXY, 
            naxis1=naxis1, 
            naxis2=naxis2,
            fields=self.fields, 
            ftName=ftName, 
            ftAName=ftAName, 
            ftBName=ftBName, 
            ftCName=ftCName,
            assocFn=assocFn
            )

        try:
            i_flux = dataSxtr.fields.index('FLUX_MAX')
            dataSxtr.fillFlux(i_flux=i_flux, logger=self.logger) #
        except Exception, e:
            if self.debug: self.logger.debug( 'sextract: no FLUX_MAX available: {0}, error: {1}'.format(fitsFn, e))

        if self.debug: self.logger.debug( 'sextract:  {0}, FOC_POS: {1:5.0f}, SX cleaned objects: {2:4d}, nstars {3:4d}, FWHM: {4:5.1f}, stdFwhm: {5:5.1f}'.format(fitsFn, focPos, len(sex.cleanedObjects), nstars, fwhm, stdFwhm))

        return dataSxtr
