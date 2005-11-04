#ifndef __RTS2_IMGSET__
#define __RTS2_IMGSET__

#include "../writers/rts2imagedb.h"

#include <iostream>
#include <vector>

class Rts2Obs;

/**
 * Class for accesing images.
 *
 * Compute statistics on images, allow easy access to images,..
 *
 * @author petr
 */
class Rts2ImgSet:public
  std::vector <
Rts2ImageDb * >
{
private:
  Rts2Obs *
    observation;
  float
    img_alt;
  float
    img_az;
  float
    img_err;
  float
    img_err_ra;
  float
    img_err_dec;

  int
    count;
  int
    astro_count;
public:
  Rts2ImgSet ();
  Rts2ImgSet (Rts2Obs * in_observation);
  int
  load ();

  friend
    std::ostream &
  operator << (std::ostream & _os, Rts2ImgSet & img_set);
};

std::ostream & operator << (std::ostream & _os, Rts2ImgSet & img_set);

#endif /* !__RTS2_IMGSET__ */
