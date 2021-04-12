// These are functions created specifically for the flask program.
#include "flask_aux.hpp"
#include <gsl/gsl_math.h>
#include <gsl/gsl_min.h>
#include <xcomplex.h>
#include "fitsfunctions.hpp"
#include "GeneralOutput.hpp"
#include <alm_healpix_tools.h>
#include "lognormal.hpp"       // For gmu, gsigma, etc. in PrintMapsStats function.
#include "definitions.hpp"     // For field types in CountLensingFields.


// Organize catalogue angular coordinates and header:
void OrganizeAngularCoord(int *AngularCoord, int *phi_pos, int *theta_pos, int *ra_pos, int *dec_pos, std::string & Header) {
  // Allow Change of Coordinates if RA and DEC were set as catalog columns:
  // For the catalog, ra, dec, theta, phi in CATALOG_COLS overrides ANGULAR_COORD. 

  // Only RA DEC were asked for:
  if (*phi_pos==-1 && *theta_pos==-1 && *ra_pos==-1 && *dec_pos!=-1)      {*AngularCoord=2; *theta_pos = *dec_pos;                   }
  else if (*phi_pos==-1 && *theta_pos==-1 && *ra_pos!=-1 && *dec_pos==-1) {*AngularCoord=2;                      *phi_pos = *ra_pos; }
  else if (*phi_pos==-1 && *theta_pos==-1 && *ra_pos!=-1 && *dec_pos!=-1) {*AngularCoord=2; *theta_pos = *dec_pos; *phi_pos = *ra_pos; }
  // Only theta phi were asked for:
  else if (*phi_pos==-1 && *theta_pos!=-1 && *ra_pos==-1 && *dec_pos==-1 && *AngularCoord==2) {
    warning("OrganizeAngularCoord: CATALOG_COLS 'theta' will be given in degrees"); *AngularCoord=1; 
  }
  else if (*phi_pos!=-1 && *theta_pos==-1 && *ra_pos==-1 && *dec_pos==-1 && *AngularCoord==2) {
    warning("OrganizeAngularCoord: CATALOG_COLS 'phi' will be given in degrees"); *AngularCoord=1; 
  }
  else if (*phi_pos!=-1 && *theta_pos!=-1 && *ra_pos==-1 && *dec_pos==-1 && *AngularCoord==2) {
    warning("OrganizeAngularCoord: CATALOG_COLS 'theta phi' will be given in degrees"); *AngularCoord=1; 
  }
  // Asked for mixed coordinates:
  //                   v                               v
  else if (*phi_pos==-1 && *theta_pos!=-1 && *ra_pos==-1 && *dec_pos!=-1 && *AngularCoord!=2) {
    warning("OrganizeAngularCoord: found mixed coordinates, 'dec' will be ignored and 'theta' catalog column will obey ANGULAR_COORD.");
    StrReplace(Header, "dec", "");
  }
  //                   v                               v 
  else if (*phi_pos==-1 && *theta_pos!=-1 && *ra_pos==-1 && *dec_pos!=-1 && *AngularCoord==2) {
    warning("OrganizeAngularCoord: found mixed coordinates, 'theta' will be ignored and 'dec' catalog column will be used.");
    *theta_pos = *dec_pos;
    StrReplace(Header, "theta", "");
  }
  //                   v                v
  else if (*phi_pos==-1 && *theta_pos!=-1 && *ra_pos!=-1 && *dec_pos==-1 && *AngularCoord!=2) {
    warning("OrganizeAngularCoord: found mixed coordinates, 'ra' catalog column will be treated as 'phi'.");
    *phi_pos = *ra_pos;
    StrReplace(Header, "ra", "phi");
  }
  //                   v                v
  else if (*phi_pos==-1 && *theta_pos!=-1 && *ra_pos!=-1 && *dec_pos==-1 && *AngularCoord==2) {
    warning("OrganizeAngularCoord: found mixed coordinates, 'theta' catalog column will be treated as 'dec'.");
    *phi_pos = *ra_pos;
    StrReplace(Header, "theta", "dec");
  }
  //                   v                v              v
  else if (*phi_pos==-1 && *theta_pos!=-1 && *ra_pos!=-1 && *dec_pos!=-1) {
    warning("OrganizeAngularCoord: found mixed coordinates, 'theta' will be ignored.");
    *theta_pos    = *dec_pos;
    *phi_pos      = *ra_pos;
    *AngularCoord = 2;
    StrReplace(Header, "theta", "");
  }
  //     v                                             v
  else if (*phi_pos!=-1 && *theta_pos==-1 && *ra_pos==-1 && *dec_pos!=-1 && *AngularCoord!=2) {
    warning("OrganizeAngularCoord: found mixed coordinates, 'dec' will be treated as 'theta'.");
    *theta_pos = *dec_pos;
    StrReplace(Header, "dec", "theta");
  }
  //     v                                             v
  else if (*phi_pos!=-1 && *theta_pos==-1 && *ra_pos==-1 && *dec_pos!=-1 && *AngularCoord==2) {
    warning("OrganizeAngularCoord: found mixed coordinates, 'phi' will be treated as 'ra'.");
    *theta_pos = *dec_pos;
    StrReplace(Header, "phi", "ra");
  }
  //     v                              v              
  else if (*phi_pos!=-1 && *theta_pos==-1 && *ra_pos!=-1 && *dec_pos==-1 && *AngularCoord==2) {
    warning("OrganizeAngularCoord: found mixed coordinates, 'phi' will be ignored.");
    *phi_pos = *ra_pos;
    StrReplace(Header, "phi", "");
  }
  //     v                              v              
  else if (*phi_pos!=-1 && *theta_pos==-1 && *ra_pos!=-1 && *dec_pos==-1 && *AngularCoord!=2) {
    warning("OrganizeAngularCoord: found mixed coordinates, 'ra' will be ignored.");
    StrReplace(Header, "ra", "");
  }
  //     v                              v              v
  else if (*phi_pos!=-1 && *theta_pos==-1 && *ra_pos!=-1 && *dec_pos!=-1) {
    warning("OrganizeAngularCoord: found mixed coordinates, 'phi' will be ignored.");
    *theta_pos    = *dec_pos;
    *phi_pos      = *ra_pos;
    *AngularCoord = 2;
    StrReplace(Header, "phi", "");
  }
  //     v             v                               v
  else if (*phi_pos!=-1 && *theta_pos!=-1 && *ra_pos==-1 && *dec_pos!=-1) {
    warning("OrganizeAngularCoord: found mixed coordinates, 'dec' will be ignored.");
    StrReplace(Header, "dec", "");
    if (*AngularCoord==2) { warning("OrganizeAngularCoord: 'theta' 'phi' will be given in degrees."); *AngularCoord=1; }
    
  }
  //     v             v                v               
  else if (*phi_pos!=-1 && *theta_pos!=-1 && *ra_pos!=-1 && *dec_pos==-1) {
    warning("OrganizeAngularCoord: found mixed coordinates, 'ra' will be ignored.");
    StrReplace(Header, "ra", "");
    if (*AngularCoord==2) { warning("OrganizeAngularCoord: 'theta' 'phi' will be given in degrees."); *AngularCoord=1; }  
  }
  //     v             v                v              v 
  else if (*phi_pos!=-1 && *theta_pos!=-1 && *ra_pos!=-1 && *dec_pos!=-1) {
    if (*AngularCoord==2) { 
      warning("OrganizeAngularCoord: found mixed coordinates, will use 'ra' 'dec'.");
      *phi_pos   = *ra_pos;
      *theta_pos = *dec_pos;
      StrReplace(Header, "phi", "");
      StrReplace(Header, "theta", "");
    }
    else {
      warning("OrganizeAngularCoord: found mixed coordinates, will use 'theta' 'phi'.");
      StrReplace(Header, "ra", "");
      StrReplace(Header, "dec", "");
    }
  }  
}


// Count number of lensing fields with different field ('f') names:
int CountLensingFields(const FZdatabase & fieldlist) {
  int k=0, f;
  for(f=0; f<fieldlist.Nfs(); f++) if (fieldlist.ftype(fieldlist.fFixedIndex(f, 0))==flensing) k++;
  return k;
}

// Count number of galaxy fields with different field ('f') names:
int CountGalaxyFields(const FZdatabase & fieldlist) {
  int k=0, f;
  for(f=0; f<fieldlist.Nfs(); f++) if (fieldlist.ftype(fieldlist.fFixedIndex(f, 0))==fgalaxies) k++;
  return k;
}


void TabulateKappaWeight(double **KappaWeightTable, const Cosmology & cosmo, const FZdatabase & fieldlist) {
  int Nfields, i, j;

  Nfields = fieldlist.Nfields();
  for (i=0; i<Nfields; i++) 
    for (j=0; j<Nfields; j++) 
      KappaWeightTable[i][j] = KappaWeightByZ(cosmo, (fieldlist.zmin(j)+fieldlist.zmax(j))/2.0, fieldlist.zmax(i)) 
	* (fieldlist.zmax(j)-fieldlist.zmin(j));
}


// Change angular coordinates in catalog if requested:
void ChangeCoord(CAT_PRECISION **catalog, int theta_pos, int phi_pos, long Ngalaxies, int coordtype) {
  long kl;
  
  if ((theta_pos!=-1 || phi_pos!=-1) && coordtype!=0) {
    Announce("Changing angular coordinates... ");
    // Only change units:
    if (coordtype==1) {
      if (theta_pos!=-1) // Theta:
#pragma omp parallel for
	for (kl=0; kl<Ngalaxies; kl++) catalog[theta_pos][kl] = rad2deg(catalog[theta_pos][kl]);
      if (phi_pos  !=-1) // Phi:
#pragma omp parallel for
	for (kl=0; kl<Ngalaxies; kl++) catalog[  phi_pos][kl] = rad2deg(catalog[  phi_pos][kl]);    
    }
    // Change to RADEC:
    else if (coordtype==2) {
      if (theta_pos!=-1) // Theta:
#pragma omp parallel for
	for (kl=0; kl<Ngalaxies; kl++) catalog[theta_pos][kl] = theta2dec(catalog[theta_pos][kl]);
      if (phi_pos  !=-1) // Phi:
#pragma omp parallel for
	for (kl=0; kl<Ngalaxies; kl++) catalog[  phi_pos][kl] =    phi2ra(catalog[  phi_pos][kl]);    
    }
    else if (coordtype!=0) warning("ChangeCoord: unknown ANGULAR_COORD option, will keep Theta & Phi in radians.");
    Announce();
  }
}

bool ComputeShearQ(const ParameterList & config) {
  std::string CatalogItems, CatalogHeader, ExitAt;
  
  // Only check if output is required if code does not stop before them:
  ExitAt = config.reads("EXIT_AT");
  if (ExitAt!="FLIST_OUT"       && ExitAt!="XIOUT_PREFIX"    && ExitAt!="GXIOUT_PREFIX"   && 
      ExitAt!="GCLOUT_PREFIX"   && ExitAt!="COVL_PREFIX"     && ExitAt!="REG_COVL_PREFIX" && 
      ExitAt!="REG_CL_PREFIX"   && ExitAt!="CHOLESKY_PREFIX" && ExitAt!="AUXALM_OUT"      && 
      ExitAt!="AUXMAP_OUT"      && ExitAt!="RECOVAUXCLS_OUT" && ExitAt!="MAP_OUT"         && 
      ExitAt!="MAPFITS_PREFIX"  && ExitAt!="DENS2KAPPA_STAT" && ExitAt!="RECOVALM_OUT"    && 
      ExitAt!="RECOVCLS_OUT") {

    // Compute shear if any shear output is required:
    if (config.reads("SHEAR_ALM_PREFIX")  !="0") return 1;
    if (config.reads("SHEAR_FITS_PREFIX") !="0") return 1;
    if (config.reads("SHEAR_MAP_OUT")     !="0") return 1;
    if (config.reads("ELLIP_MAP_OUT")     !="0") return 1;
    if (config.reads("ELLIPFITS_PREFIX")  !="0") return 1;
    // Compute shear if it goes into catalog:
    if (ExitAt=="CATALOG_OUT" || ExitAt=="0") {
      CatalogItems = config.reads("CATALOG_COLS");
      if (GetSubstrPos("gamma1" , CatalogItems) != -1) return 1;
      if (GetSubstrPos("gamma2" , CatalogItems) != -1) return 1;
      if (GetSubstrPos("ellip1" , CatalogItems) != -1) return 1;
      if (GetSubstrPos("ellip2" , CatalogItems) != -1) return 1;
    }
  } // End of IF stop code before shear output.

  // If no shear related output is requested, return 0:
  return 0;
}


double MapMean(const Healpix_Map<MAP_PRECISION> & map) {
  return map.average();
}

double MapVariance(const Healpix_Map<MAP_PRECISION> & map, double mean) {
  int i, npixels;
  double var=0.0, aux;
  
  npixels = map.Npix();
#pragma omp parallel for reduction(+:var) private(aux)
  for (i=0; i<npixels; i++) {
    aux  = map[i] - mean;
    var += aux*aux;
  }
  return var/npixels;
}

double MapSkewness(const Healpix_Map<MAP_PRECISION> & map, double mean, double variance) {
  int i, npixels;
  double skew=0.0, aux;
  
  npixels = map.Npix();
#pragma omp parallel for reduction(+:skew) private(aux)
  for (i=0; i<npixels; i++) {
    aux  = map[i] - mean;
    skew += pow(aux, 3);
  }
  return skew/npixels/pow(variance, 1.5);
}


// Prints a table with mean, std. dev., skewness, mu (gaussian mean), sigma (gaussian dev) and shift for all maps.
void PrintMapsStats(Healpix_Map<MAP_PRECISION> *mapf, const FZdatabase & fieldlist, simtype dist, std::ostream *output) {
  const int ColWidth = 12;
  int Nfields, i, f, z;
  char name[11];
  double mean, var, skew, shift;
  
  Nfields = fieldlist.Nfields();
  (*output).width(ColWidth);
  
  // Header:
  (*output) <<std::left<<"# FieldID"<<std::right; (*output).width(ColWidth);
  (*output) << "Mean";           (*output).width(ColWidth);
  (*output) << "Std.Dev.";       (*output).width(ColWidth);
  (*output) << "Skewness";       (*output).width(ColWidth);
  if (dist==lognormal) {
    (*output) << "gMU";          (*output).width(ColWidth);
    (*output) << "gSIGMA";       (*output).width(ColWidth);
    (*output) << "Shift";        (*output).width(ColWidth);
  }
  (*output) << std::endl;        (*output).width(ColWidth);

  // LOOP over allocated maps: 
  for (i=0; i<Nfields; i++) if (mapf[i].Nside()>0) {
      // Compute statistics:
      mean  = MapMean(mapf[i]);
      var   = MapVariance(mapf[i], mean);
      skew  = MapSkewness(mapf[i], mean, var);
      shift = Moments2Shift(mean, var, skew); 
      // Output results:
      fieldlist.Index2Name(i, &f, &z);
      sprintf(name, "f%dz%d   ",f,z);
      (*output) << std::left<<name<<std::right; (*output).width(ColWidth);
      (*output) << mean;                        (*output).width(ColWidth);
      (*output) << sqrt(var);                   (*output).width(ColWidth);
      (*output) << skew;                        (*output).width(ColWidth);
      if (dist==lognormal) {
	(*output) << gmu(mean, var, shift);     (*output).width(ColWidth);
	(*output) << gsigma(mean, var, shift);  (*output).width(ColWidth);
	(*output) << shift;                     (*output).width(ColWidth);
      }
      (*output) << std::endl;                   (*output).width(ColWidth);
    }
}


// This routine takes an array of alm's, computes the Cls for each one of them and output the results:
void RecoverCls(Alm<xcomplex <ALM_PRECISION> > *bflm, const FZdatabase & fieldlist, 
		std::string clsKey, const ParameterList & config) {
  int i, j, k, l, m, lmin, lmax, lminout, lmaxout, mmax, NCls, Nfields;
  double **recovCl;
  bool *yesCl;

  if (config.reads(clsKey)!="0") {

    Nfields = fieldlist.Nfields();
    lmin    = config.readi("LRANGE", 0);
    lmax    = config.readi("LRANGE", 1);  
    lminout = config.readi("LRANGE_OUT", 0);
    lmaxout = config.readi("LRANGE_OUT", 1);
    if (lmin > lmax) error("RecoverCls: LRANGE set in the wrong order.");
    if (lminout > lmaxout) error("RecoverCls: LRANGE_OUT set in the wrong order.");
    if (lmaxout > lmax) { lmaxout=lmax; warning("RecoverCls: LRANGE_OUT beyond LRANGE upper bound, will use the latter instead."); }
    if (lminout < lmin) { lminout=lmin; warning("RecoverCls: LRANGE_OUT beyond LRANGE lower bound, will use the latter instead."); }

    Announce("Recovering Cl's from alm's... ");
    mmax    = config.readi("MMAX_OUT");
    if (mmax>lminout) error("RecoverCls: current code only allows MMAX_OUT <= LRANGE_OUT lower bound.");
    NCls    = Nfields*(Nfields+1)/2;
    recovCl = matrix<double>(0, NCls-1, lminout, lmaxout);
    yesCl   = vector<bool>(0, NCls-1);
#pragma omp parallel for schedule(dynamic) private(l, m, i, j)
    for (k=0; k<NCls; k++) {
      l = (int)((sqrt(8.0*(NCls-1-k)+1.0)-1.0)/2.0);
      m = NCls-1-k-(l*(l+1))/2;
      i = Nfields-1-l;
      j = Nfields-1-m;
      if(bflm[i].Lmax()!=0 && bflm[j].Lmax()!=0) {
	yesCl[k] = 1;
	for(l=lminout; l<=lmaxout; l++) {
	  recovCl[k][l] = 0;
#if USEXCOMPLEX // For compatibility with Healpix versions <=3.20 and >=3.30.
	  if (mmax<0) for (m=0; m<=l; m++)    recovCl[k][l] += (bflm[i](l,m)*(bflm[j](l,m).conj())).real();
	  else        for (m=0; m<=mmax; m++) recovCl[k][l] += (bflm[i](l,m)*(bflm[j](l,m).conj())).real();
#else
	  if (mmax<0) for (m=0; m<=l; m++)    recovCl[k][l] += (bflm[i](l,m)*conj(bflm[j](l,m))).real();
	  else        for (m=0; m<=mmax; m++) recovCl[k][l] += (bflm[i](l,m)*conj(bflm[j](l,m))).real();
#endif
	  recovCl[k][l] = recovCl[k][l]/((double)(l+1));
	}
      }
      else yesCl[k] = 0;
    } // End of Cl computing.
    Announce();

    GeneralOutput(recovCl, yesCl, fieldlist, config, clsKey);
    free_matrix(recovCl, 0, NCls-1, lminout, lmaxout);
    free_vector(yesCl, 0, NCls-1);
  }
}


// If alm or Cls output is requested, compute them and output them:
void RecoverAlmCls(Healpix_Map<MAP_PRECISION> *mapf, const FZdatabase & fieldlist, 
		   std::string almKey, std::string clsKey, const ParameterList & config) {
  int i, l, m, nside, lmin, lmax, lminout, lmaxout, Nfields;
  Alm<xcomplex <ALM_PRECISION> > *bflm;

  if (config.reads(almKey)!="0" || config.reads(clsKey)!="0") {

    Nfields = fieldlist.Nfields();
    nside   = config.readi("NSIDE");
    lmin    = config.readi("LRANGE", 0);
    lmax    = config.readi("LRANGE", 1);  
    lminout = config.readi("LRANGE_OUT", 0);
    lmaxout = config.readi("LRANGE_OUT", 1);
    if (lmin > lmax) error("RecoverAlmCls: LRANGE set in the wrong order.");
    if (lminout > lmaxout) error("RecoverAlmCls: LRANGE_OUT set in the wrong order.");
    if (lmaxout > lmax) { lmaxout=lmax; warning("RecoverAlmCls: LRANGE_OUT beyond LRANGE upper bound, will use the latter instead."); }
    if (lminout < lmin) { lminout=lmin; warning("RecoverAlmCls: LRANGE_OUT beyond LRANGE lower bound, will use the latter instead."); }

    // Allocate and clear variables to receive new alm's:
    bflm  = vector<Alm<xcomplex <ALM_PRECISION> > >(0,Nfields-1);
    for (i=0; i<Nfields; i++) if(mapf[i].Nside()!=0) {
	bflm[i].Set(lmaxout,lmaxout);
	for(l=0; l<=lmaxout; l++) for (m=0; m<=l; m++) {
#if USEXCOMPLEX // For compatibility with Healpix versions <=3.20 and >=3.30.
	    bflm[i](l,m).Set(0,0);
#else
	    bflm[i](l,m).real(0);
	    bflm[i](l,m).imag(0);	    
#endif
	  }
      }    
    // Load map ring weights:
    arr<double> weight(2*nside);
    PrepRingWeights(1, weight, config);
    // Compute alm's from map:
    Announce("Recovering alm's from map... ");
    for(i=0; i<Nfields; i++) if(mapf[i].Nside()!=0) map2alm(mapf[i],bflm[i],weight);
    Announce();
    weight.dealloc();
    // Output to file:
    GeneralOutput(bflm, config, almKey, fieldlist);

    // Compute Cl's if requested:
    RecoverCls(bflm, fieldlist, clsKey, config);

    // Free memory:
    free_vector(bflm, 0, Nfields-1);
  }
}


// Prepare weights used by map2alm. weight array must be allocated already:
void PrepRingWeights(int col, arr<double> & weight, const ParameterList & config) {
  double *tempweight;
  int nside, status;
  long i;

  nside = config.readi("NSIDE");

  if (config.readi("USE_HEALPIX_WGTS")==1) {
    Announce("   Loading Healpix map weights... ");
    tempweight = vector<double>(0, 2*nside-1);
    status = ReadHealpixData(col, config, tempweight, 1);
    if (status==0) for (i=0; i<2*nside; i++) weight[i]=1.0+tempweight[i];
    else { 
      warning("PrepRingWeights: could not load Healpix weights, using 1.0 instead.");
      weight.fill(1);
    }
    free_vector(tempweight, 0, 2*nside-1);
    Announce();
  }
  else weight.fill(1);  
}


// Transform radians to degrees:
double rad2deg(double rad) {
  const double OneEightyOverPi = 180.0/M_PI;
  return OneEightyOverPi * rad;
}

// Transform Theta (in radians) into DEC (in degrees):
double theta2dec(double theta) {
  const double PiOverTwo = M_PI/2.0;
  return rad2deg(PiOverTwo - theta);
}

// Transform Phi (in radians) into RA (in degrees):
double phi2ra(double phi) {
  return rad2deg(phi);
}


// Get file format from filename extension: .dat, .fits, .fits.gz
int FileFormat(std::string filename) {
  using namespace definitions;
  int pos;
  
  // Possible file extensions:
  pos = filename.rfind(".fits.gz");
  if (pos>0 && pos+8==filename.size()) return fits_format;
  pos = filename.rfind(".fits");
  if (pos>0 && pos+5==filename.size()) return fits_format;
  pos = filename.rfind(".dat");
  if (pos>0 && pos+4==filename.size()) return ascii_format;
  
  // Unidentified format:
  return unknown_format;
}


// Count words in phrase:
int CountWords(const std::string header) {
  std::string entry;
  std::stringstream ss(header);
  int column=0;

  while (ss >> entry) column++;
  return column;
}


// Return position of word in phrase, return -1 if word is not found:
int GetSubstrPos(const std::string field, const std::string header) {
  std::string entry;
  std::stringstream ss(header);
  int column=0;

  while (ss >> entry && entry!=field) column++;
  if (entry!=field) return -1;
  return column;
}


// Unless column=-1, write value to catalog[column][row] and update catSet:
// Note that the catalog is transposed to ease FITS outputting.
void CatalogFill(CAT_PRECISION **catalog, long row, int column, double value, char **catSet) {  
  if(column==-1) return;
  
  // Write to catalog:            v Count number of updates in cell (for bookkeeping).
  catalog[column][row] = value;   catSet[column][row]++;
}

// Unless column=-1, write value to catalog[column][row] and update catSet:
// Note that the catalog is transposed to ease FITS outputting.
void CatalogFill(CAT_PRECISION **catalog, long row, int column, double value) {  
  if(column==-1) return;
  
  // Write to catalog:            
  catalog[column][row] = value;
}


// Get the shear E-mode harmonic coefficients from the convergence harmonic coefficients:
// (can be done in place)
void Kappa2ShearEmode(Alm<xcomplex <ALM_PRECISION> > &Elm, Alm<xcomplex <ALM_PRECISION> > &Klm) {
  int l, m, lmax;
  double coeff;

  if (Elm.Lmax()!=Klm.Lmax()) error("Kappa2ShearEmode: Elm and klm must have the same lmax.");
  if (Elm.Mmax()!=Klm.Mmax()) error("Kappa2ShearEmode: Elm and klm must have the same mmax.");  
  lmax = Klm.Lmax();
  
  // E mode monopole and dipole are zero:
  for(l=0; l<2; l++) for (m=0; m<=l; m++) {
#if USEXCOMPLEX // For compatibility with Healpix versions <=3.20 and >=3.30.      
      Elm(l,m).Set(0,0);
#else
      Elm(l,m).real(0);
      Elm(l,m).imag(0);
#endif
    }

  // Use Wayne Hu (PRD 62:043007, 2000) to get Elm from klm:
  for(l=2; l<=lmax; l++) { 
    coeff = sqrt( ((double)((l+2)*(l-1))) / ((double)(l*(l+1))) );
    for (m=0; m<=l; m++) {
#if USEXCOMPLEX // For compatibility with Healpix versions <=3.20 and >=3.30.      
      Elm(l,m).Set(coeff*Klm(l,m).re,coeff*Klm(l,m).im);
#else
      Elm(l,m).real(coeff*Klm(l,m).real());
      Elm(l,m).imag(coeff*Klm(l,m).imag());      
#endif
    }
  }
}

// Transforms normalized {x,y,z} cartesian coordinates to unit spherical {theta, phi}:
pointing xyz2ang(const vec3 & cartesian) {
  const double pi=3.141592653589793;
  double tempphi;
  pointing ang;
  ang.theta = acos(cartesian.z);
  tempphi   = atan(cartesian.y/cartesian.x);
  if   (cartesian.x<0)    ang.phi = tempphi + pi;   // Quadrants 2 & 3.
  else {
    if (cartesian.y>0)    ang.phi = tempphi;        // Quadrant  1.
    else                  ang.phi = tempphi + pi*2; // Quadrant  4.
  }
  return ang;
}

/*** Multiply Lower-triangular matrix L to complex vector gaus0 and return gaus1 ***/
void CorrGauss(double **gaus1, gsl_matrix *L, double **gaus0) {
  long i, j;

  for(i=0; i<L->size1; i++) {
    gaus1[i][0]=0; // Re    
    gaus1[i][1]=0; // Im
    for(j=0; j<=i; j++) { // L matrix stored as vector in row-major order.
      gaus1[i][0] += L->data[i*L->size1+j] * gaus0[j][0];
      gaus1[i][1] += L->data[i*L->size1+j] * gaus0[j][1];
    }
  }
}
