#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>
#include <unistd.h>

#include "sep/sep.h"

#include <check.h>
#include <check_utils.h>

void setup_sep (void)
{
	unlink ("data/fram.out");
	unlink ("data/image.out");
}


void teardown_sep (void)
{
}


uint64_t
gettime_ns ()
{
	struct timeval tv;
	gettimeofday (&tv, NULL);
	return (uint64_t) tv.tv_sec * 1000000000ULL + tv.tv_usec * 1000ULL;
}


double *
ones_dbl (int nx, int ny)
{
	int i, npix;
	double *im, *imt;

	im = (double *) malloc ((npix = nx * ny) * sizeof (double));
	imt = im;
	for (i = 0; i < npix; i++, imt++)
		*imt = 1.0;

	return im;
}


float *
uniformf (float a, float b, int n)
/* an array of n random numbers from the uniform interval (a, b) */
{
	int i;
	float *result;

	result = (float *) malloc (n * sizeof (float));
	for (i = 0; i < n; i++)
		result[i] = a + (b - a) * rand () / ((double) RAND_MAX);

	return result;
}


double
gaussrand ()
{
	static double V1, V2, S;
	static int phase = 0;
	double x, u1, u2;

	if (phase == 0)
	{
		do
		{
			u1 = (double) rand () / RAND_MAX;
			u2 = (double) rand () / RAND_MAX;

			V1 = 2. * u1 - 1.;
			V2 = 2. * u2 - 1.;
			S = V1 * V1 + V2 * V2;
		}
		while (S >= 1. || S == 0.);

		x = V1 * sqrt (-2. * log (S) / S);
	}
	else
	{
		x = V2 * sqrt (-2. * log (S) / S);
	}

	phase = 1 - phase;

	return x;
}


float *
makenoiseim (int nx, int ny, float mean, float sigma)
{
	int i, npix;
	float *im, *imt;

	im = (float *) malloc ((npix = nx * ny) * sizeof (float));

	/* fill with noise */
	imt = im;
	for (i = 0; i < npix; i++, imt++)
		*imt = mean + sigma * (float) gaussrand ();

	return im;
}


float *
ones (int nx, int ny)
{
	int i, npix;
	float *im, *imt;

	im = (float *) malloc ((npix = nx * ny) * sizeof (float));
	imt = im;
	for (i = 0; i < npix; i++, imt++)
		*imt = 1.0;

	return im;
}


void
addbox (float *im, int w, int h, float xc, float yc, float r, float val)
/* n = sersic index */
{
	int xmin, xmax, ymin, ymax;
	int x, y;

	int rmax = (int) r;

	xmin = (int) xc - rmax;
	xmin = (xmin < 0) ? 0 : xmin;
	xmax = (int) xc + rmax;
	xmax = (xmax > w) ? w : xmax;
	ymin = (int) yc - rmax;
	ymin = (ymin < 0) ? 0 : ymin;
	ymax = (int) yc + rmax;
	ymax = (ymax > h) ? h : ymax;

	for (y = ymin; y < ymax; y++)
		for (x = xmin; x < xmax; x++)
			im[x + w * y] += val;

}


void
printbox (float *im, int w, int h, int xmin, int xmax, int ymin, int ymax)
/* print image values to the screen in a grid

   Don't make box size too big!!
*/
{
	int i, j;

	for (j = ymin; j < ymax && j < h; j++)
	{
		for (i = xmin; i < xmax && i < w; i++)
			printf ("%6.2f ", im[w * j + i]);
		printf ("\n");
	}
}


/* an extremely dumb reader for our specific test FITS file! */
int
read_test_image (const char *fname, float **data, int *nx, int *ny)
{
	FILE *f;
	char buf[80];				 /* buffer to hold a line */
	long pos;
	size_t nbytes, nel, i, elsize, nread;
	char *rawbytes = NULL;
	short *rawdata;
	char tmp;
	int status = 0;
	float bscale, bzero;

	/* hard-code image size & element size */
	*nx = 256;
	*ny = 256;
	elsize = 2;
	nel = *nx * *ny;
	bscale = 2.92273835509;
	bzero = 3713.66692596;

	f = fopen (fname, "r");

	/* read first line and check if it is a FITS file */
	nread = fread (buf, 1, 80, f);
	if (nread != 80)
	{
		status = 1;
		goto exit;
	}
	if (strncmp (buf, "SIMPLE  =                    T", 30) != 0)
	{
		printf ("not a FITS file");
		status = 1;
		goto exit;
	}

	/* read rows until we get to END keyword */
	while (strncmp (buf, "END", 3) != 0)
	{
		nread = fread (buf, 1, 80, f);
		if (nread != 80)
		{
			status = 1;
			goto exit;
		}
	}

	/* move to next 2880 byte boundary. */
	pos = ftell (f) % 2880;		 /* position in "page" */
	if (pos != 0)
		fseek (f, 2880 - pos, SEEK_CUR);

	/* read raw data bytes */
	nbytes = nel * elsize;
	rawbytes = (char *) malloc (nbytes);
	nread = fread (rawbytes, 1, nbytes, f);
	if (nread != nbytes)
	{
		status = 1;
		goto exit;
	}

	/* swap bytes in raw data (FITS is big-endian) */
	for (i = 0; i < nbytes; i += 2)
	{
		tmp = rawbytes[i];
		rawbytes[i] = rawbytes[i + 1];
		rawbytes[i + 1] = tmp;
	}
	rawdata = (short *) rawbytes;/* buffer is now little-endian */

	/* convert to float, applying bscale/bzero */
	*data = (float *) malloc (nel * sizeof (float));
	for (i = 0; i < nel; i++)
	{
		(*data)[i] = bscale * rawdata[i] + bzero;
	}

	exit:
	free (rawbytes);

	return status;
}


float *
tile_flt (float *im, int nx, int ny, int ntilex, int ntiley,
int *nxout, int *nyout)
{
	int i, x, y;
	int npixout;
	float *imout;

	*nxout = ntilex * nx;
	*nyout = ntiley * ny;
	npixout = *nxout * *nyout;

	imout = (float *) malloc (npixout * sizeof (float));
	for (i = 0; i < npixout; i++)
	{
		x = (i % (*nxout)) % nx; /* corresponding x on small im */
		y = (i / (*nxout)) % ny; /* corresponding y on small im */
		imout[i] = im[y * nx + x];
	}
	return imout;
}


void
print_time (const char *s, uint64_t tdiff)
{
	printf ("%-25s%6.1f ms\n", s, (double) tdiff / 1000000.);
}


void
check_image(const char *in, const char *out, sep_catalog **catalog)
{
	int i, status, nx, ny;
	double *flux, *fluxerr, *fluxt, *fluxerrt, *area, *areat;
	short *flag, *flagt;
	float *data, *imback;
	uint64_t t0, t1;
	sep_bkg *bkg = NULL;
	float conv[] = { 1, 2, 1, 2, 4, 2, 1, 2, 1 };
	sep_catalog *cat;
	FILE *catout;
	sep_image im;

	*catalog = NULL;

	status = 0;
	flux = fluxerr = NULL;
	flag = NULL;

	/* read in image */
	status = read_test_image (in, &data, &nx, &ny);
	if (status)
		goto exit_test;

	/* test the version string */
	printf ("sep version: %s\n", sep_version_string);

	/* background estimation */
	t0 = gettime_ns ();
	im =
	{
		data, NULL, NULL, SEP_TFLOAT, 0, 0, nx, ny, 0.0, SEP_NOISE_NONE, 1.0, 0.0
	};
	status = sep_background (&im, 64, 64, 3, 3, 0.0, &bkg);
	t1 = gettime_ns ();
	if (status)
		goto exit_test;
	print_time ("sep_background()", t1 - t0);

	/* evaluate background */
	imback = (float *) malloc ((nx * ny) * sizeof (float));
	t0 = gettime_ns ();
	status = sep_bkg_array (bkg, imback, SEP_TFLOAT);
	t1 = gettime_ns ();
	if (status)
		goto exit_test;
	print_time ("sep_bkg_array()", t1 - t0);

	/* subtract background */
	t0 = gettime_ns ();
	status = sep_bkg_subarray (bkg, im.data, im.dtype);
	t1 = gettime_ns ();
	if (status)
		goto exit_test;
	print_time ("sep_bkg_subarray()", t1 - t0);

	/* extract sources
	 * Note that we set deblend_cont = 1.0 to turn off deblending.
	 */
	t0 = gettime_ns ();
	status = sep_extract (&im, 1.5 * bkg->globalrms, SEP_THRESH_ABS,
		5, conv, 3, 3, SEP_FILTER_CONV,
		32, 1.0, 1, 1.0, catalog);
	cat = *catalog;
	t1 = gettime_ns ();
	if (status)
		goto exit_test;
	print_time ("sep_extract()", t1 - t0);

	printf ("number of catalogue objects: %d\n", cat->nobj);

	/* aperture photometry */
	im.noise = &(bkg->globalrms);/* set image noise level */
	im.ndtype = SEP_TFLOAT;
	fluxt = flux = (double *) malloc (cat->nobj * sizeof (double));
	fluxerrt = fluxerr = (double *) malloc (cat->nobj * sizeof (double));
	areat = area = (double *) malloc (cat->nobj * sizeof (double));
	flagt = flag = (short *) malloc (cat->nobj * sizeof (short));
	t0 = gettime_ns ();
	for (i = 0; i < cat->nobj; i++, fluxt++, fluxerrt++, flagt++, areat++)
		sep_sum_circle (&im,
			cat->x[i], cat->y[i], 5.0, 5, 0,
			fluxt, fluxerrt, areat, flagt);
	t1 = gettime_ns ();
	printf ("sep_sum_circle() [r= 5.0]  %6.3f us/aperture\n",
		(double) (t1 - t0) / 1000. / cat->nobj);

	/* print results */
	printf ("writing to file: %s\n", out);
	catout = fopen (out, "w+");
	fprintf (catout, "# SEP catalog\n");
	fprintf (catout, "# 1 NUMBER\n");
	fprintf (catout, "# 2 X_IMAGE (0-indexed)\n");
	fprintf (catout, "# 3 Y_IMAGE (0-indexed)\n");
	fprintf (catout, "# 4 FLUX\n");
	fprintf (catout, "# 5 FLUXERR\n");
	fprintf (catout, "# 5 FLUXERR\n");
	fprintf (catout, "# 6 X2_IMAGE (seconds moments)\n");
	fprintf (catout, "# 7 Y2_IMAGE\n");
	fprintf (catout, "# 8 XY_IMAGE\n");
	fprintf (catout, "# 9 a\n");
	fprintf (catout, "# 10 b\n");
	fprintf (catout, "# 11 theta\n");
	for (i = 0; i < cat->nobj; i++)
	{
		fprintf (catout,
			"%3d %#11.7g %#11.7g %#11.7g %#11.7g %#11.7g %#11.7g %#11.7g %#11.7g %#11.7g %#11.7g\n",
			i, cat->x[i], cat->y[i], flux[i], fluxerr[i],
			cat->x2[i], cat->y2[i], cat->xy[i],
			cat->a[i], cat->b[i], cat->theta[i]);
	}
	fclose (catout);

	/* clean-up & exit */
	exit_test:
	sep_bkg_free (bkg);
	free (data);
	free (flux);
	free (fluxerr);
	free (flag);
	if (status)
	{
		printf ("FAILED with status: %d\n", status);
		char errtext[512];
		sep_get_errdetail (errtext);
		puts (errtext);
	}
	else
	{
		printf ("test_image passed\n");
	}
	ck_assert_int_eq (status, 0);
}


START_TEST(SEP1)
{
	sep_catalog *catalog;
	check_image ("data/fram.fits", "data/fram.out", &catalog);
	check_image ("data/image.fits", "data/image.out", &catalog);

	ck_assert_int_eq (catalog->nobj, 65);
}
END_TEST

/***************************************************************************/
/* aperture photometry */

/*
   int naper, j;
   float *xcs, *ycs;

   im = ones(nx, ny);
   naper = 1000;
   flux = fluxerr = 0.0;
   flag = 0;

   float rs[] = {3., 5., 10., 20.};
   for (j=0; j<4; j++)
   {
   r = rs[j];
   xcs = uniformf(2.*r, nx - 2.*r, naper);
   ycs = uniformf(2.*r, ny - 2.*r, naper);

   printf("sep_apercirc() [r=%4.1f]   ", r);
   t0 = gettime_ns();
   for (i=0; i<naper; i++)
   sep_apercirc(im, NULL, SEP_TFLOAT, nx, ny, 0.0, 0.0,
   xcs[i], ycs[i], r, 5, &flux, &fluxerr, &flag);
   t1 = gettime_ns();
   printf("%6.3f us/aperture\n", (double)(t1 - t0) / 1000. / naper);
   free(xcs);
   free(ycs);
   }
 */

Suite *
sep_suite (void)
{
	Suite *s;
	TCase *tc_sep;

	s = suite_create ("SEP");
	tc_sep = tcase_create ("SEP");

	tcase_add_checked_fixture (tc_sep, setup_sep, teardown_sep);
	tcase_add_test (tc_sep, SEP1);
	suite_add_tcase (s, tc_sep);

	return s;
}


int
main (void)
{
	int number_failed;
	Suite *s;
	SRunner *sr;

	s = sep_suite ();
	sr = srunner_create (s);
	srunner_run_all (sr, CK_VERBOSE);
	number_failed = srunner_ntests_failed (sr);
	srunner_free (sr);

	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
