<?xml version="1.0"?>
<xsl:stylesheet version="1.0" xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<xsl:output method='text' indent='no'/>

<xsl:template match='/'>
@ imgid = 0
<xsl:apply-templates select='*'/>
</xsl:template>

<xsl:variable name='abort'>
if ( -e $rts2abort ) then
	source $RTS2/bin/.rts2-runabort
	exit
endif	
</xsl:variable>

<xsl:template match="disable">
$RTS2/bin/rts2-target -d $tar_id
</xsl:template>

<xsl:template match="tempdisable">
$RTS2/bin/rts2-target -n +<xsl:value-of select='.'/> $tar_id
</xsl:template>

<xsl:template match="exposure">
echo `date` 'starting exposure <xsl:value-of select='@length'/>'
<xsl:copy-of select='$abort'/>
ccd gowait <xsl:value-of select='@length'/>
<xsl:copy-of select='$abort'/>
dstore
set lastimage=/rdata/`grep "cd" /tmp/iraf_logger.cl |cut -f2 -d" "`/`grep "addlog" /tmp/iraf_logger.cl |cut -f2 -d" "|sed s/\"//g`
$RTS2/bin/rts2-image -i --camera KCAM --telescope FLWO48 --obsid $obs_id --imgid $imgid $lastimage
@ imgid ++
echo `date` 'exposure done'
<xsl:copy-of select='$abort'/>
</xsl:template>

<xsl:template match="set">
<xsl:if test='@value = "filter"'>
echo -n `date` 'moving filter wheel to <xsl:value-of select='@operands'/>'
source $RTS2/bin/rts2_tele_filter <xsl:value-of select='@operands'/>
</xsl:if>
</xsl:template>

<xsl:template match="for">
@ count = 0
<xsl:copy-of select='$abort'/>
while ($count &lt; <xsl:value-of select='@count'/>)<xsl:for-each select='*'><xsl:apply-templates select='current()'/></xsl:for-each>
@ count ++

end
</xsl:template>

</xsl:stylesheet>
