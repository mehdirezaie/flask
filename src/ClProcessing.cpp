#include "ParameterList.hpp"    // Configuration and input system.
#include "Utilities.hpp"        // Error handling and memory allocation.
#include "s2kit10_naive.hpp"    // For Discrete Legendre Transforms.
#include <gsl/gsl_matrix.h>     // gsl_matrix.
#include <math.h>               // exp, log...
#include "GeneralOutput.hpp"
#include "RegularizeCov.hpp"
#include "flask_aux.hpp"        // For n2fz functions, Maximum, etc..
#include <unistd.h>             // For access function.
#include "FieldsDatabase.hpp"   
#include "fitsfunctions.hpp"    // For ReadHealpixData function used for Healpix pixel window function.
#include "Spline.hpp"           // For applying Healpix window function to arbitrarily spaced C(l)s.



/*** Multiplies a C(l) by a constant factor ***/
void ScaleCls(double *ClOut, double factor, double *ClIn, int Nls) {
  int i;
  for (i=0; i<Nls; i++)
    ClOut[i] = factor*ClIn[i];
} 


/*** Transform a C(l) to represent the 2D field now smoothed by a Gaussian with variance sigma2 ***/
void ApplyGausWinFunc(double *ClOut, double sigma2, double *l, double *ClIn, int Nls) {
  int i;
  for (i=0; i<Nls; i++)
    ClOut[i] = exp( -l[i]*(l[i]+1)*sigma2 ) * ClIn[i];
} 


/*** Transforms a correlation function of gaussian variables gXi into a corr. function of corresponding lognormal variables lnXi ***/
void GetLNCorr(double *lnXi, double *gXi, int XiLength, double mean1, double shift1, double mean2, double shift2) {
  int i;
  
  for (i=0; i<XiLength; i++) lnXi[i] = ( exp(gXi[i]) - 1.0 ) * (mean1+shift1) * (mean2+shift2);
}


/*** Transforms a correlation function of lognormal variables lnXi into a corr. function of associated gaussian variables gXi ***/
int GetGaussCorr(double *gXi, double *lnXi, int XiLength, double mean1, double shift1, double mean2, double shift2) {
  int i, status=0;
  double arg, bad=-666.0;
  char message[100];
  
  for (i=0; i<XiLength; i++) {
    arg = 1.0 + lnXi[i]/(mean1+shift1)/(mean2+shift2);
    if (arg <= 0) {
      sprintf(message, "GetGaussCorr: lnXi[%d] leads to bad log argument, gXi[%d] set to %g.", i, i, bad);
      warning(message);
      status=EDOM;
      gXi[i] = bad;
    }
    else gXi[i] = log(arg);
  }
  return status;
}


// Function that returns a Cl label according to two Fields i and j:
std::string Fields2Label(int i, int j, const FZdatabase & fieldlist) {
  int af, az, bf, bz;
  char message[100];
  std::string label;
  fieldlist.Index2Name(i, &af, &az); fieldlist.Index2Name(j, &bf, &bz); 
  sprintf(message, "Cl-f%dz%df%dz%d",af,az,bf,bz);
  label.assign(message);
  return label;
}


/*** Export function y(x) for the field combination [i,j] to file ***/
std::string PrintOut(std::string prefix, int i, int j, const FZdatabase & fieldlist, double *x, double *y, int length) {
  int af, az, bf, bz;
  char message[100];
  std::string filename;
  double *wrapper[2];
  std::ofstream outfile;

  wrapper[0] =  x;
  wrapper[1] =  y;

  fieldlist.Index2Name(i, &af, &az); fieldlist.Index2Name(j, &bf, &bz); 
  sprintf(message, "%sf%dz%df%dz%d.dat", prefix.c_str(),af,az,bf,bz);
  filename.assign(message);

  outfile.open(message);
  if (!outfile.is_open()) error("PrintOut: cannot open file "+filename);
  PrintVecs(wrapper, length, 2, &outfile);
  outfile.close();

  return filename;
}


/*** Copy a vector into a certain column of a matrix ***/
void VecInColumn(double *vec, double **matrix, int col, int Ncols, int Nrows) {
  int i;
  if (col>Ncols) error("VecInColumn: unknown column (> # of columns).");
  if (col<0)     error("VecInColumn: unknown column (< 0).");
  for (i=0; i<Nrows; i++) matrix[i][col] = vec[i];
}


/*** Wraps all processing of the input Cls up to the gaussian covariance matrices for each l ***/
int ClProcess(gsl_matrix ***CovBylAddr, int *NlsOut, FZdatabase & fieldlist, const ParameterList & config) {
  using namespace definitions;                          // Global definitions.
  using std::cout; using std::endl;                     // Basic stuff.
  const double LARGESTVARIANCE=1e12;                    // Initial value for searching for the minimum variance.
  simtype dist;                                         // For specifying simulation type.
  char message[200];                                    // Writing outputs with sprintf.
  std::ofstream outfile;                                // File for output.
  FILE* stream; int NinputCls; std::string *filelist;   // To list input Cls.
  int i, j, k, l, m, n, status, Nfields, Nf, Nz, Nls, lmin, lmax;
  std::string filename, ExitAt, prefix;
  bool *fnzSet, IsPrefix;
  gsl_matrix **CovByl;
  double temp, badcorrfrac, mindiagfrac, mindiag;
  double **auxMatrix;
  
  // Getting general information:
  Nfields = fieldlist.Nfields();
  if (config.reads("DIST")=="LOGNORMAL") dist=lognormal;
  else if (config.reads("DIST")=="GAUSSIAN") dist=gaussian;
  ExitAt  = config.reads("EXIT_AT");
  lmax    = config.readi("LRANGE", 1);
  lmin    = config.readi("LRANGE", 0);
  if (lmax<lmin) error("ClProcess: LRANGE set in the wrong order.");


  /********************************************/
  /*** PART 1: Load C(l)s and organize them ***/
  /********************************************/
  const int HWMAXL = 10000000; int lastl = HWMAXL;
  int af, az, bf, bz, **fnz, **NentMat;
  long *Nentries, ncols, Nlinput;
  double ***ll, ***Cov, **wrapper, *dump;
  bool **IsSet;

  // Get list of the necessary C(l) files:
  prefix    = config.reads("CL_PREFIX"); 
  if (prefix.length()>=4 && prefix.substr(prefix.length()-4,4)==".dat") IsPrefix=false;
  else IsPrefix=true;
  
  // CASE 1 -- Cl prefixes:
  if (IsPrefix==true) {
    NinputCls = Nfields*Nfields;  // In this case, NinputCls is determined by the number of Fields.
    filelist  = vector<std::string>(0,NinputCls-1);
    // LOOP over all C(l)s:
    for (k=0; k<NinputCls; k++) {
      i = k/Nfields;      j = k%Nfields;
      fieldlist.Index2Name(i, &af, &az);
      fieldlist.Index2Name(j, &bf, &bz);
      sprintf(message, "%sf%dz%df%dz%d.dat", prefix.c_str(), af, az, bf, bz);
      if(access(message, R_OK) == 0) filelist[k].assign(message);
    }
    
    // Find out the number of lines and columns in C(l) files:  
    Nlinput  = 0;
    Nentries = vector<long>(0,NinputCls-1);
    for (k=0; k<NinputCls; k++) {
      if (filelist[k].size()>0) {
	CountEntries(filelist[k], &(Nentries[k]), &ncols); // Get number of Nls.
	if (ncols!=2) error("ClProcess: wrong number of columns in file "+filelist[k]);
	if (Nentries[k]>Nlinput) Nlinput=Nentries[k];          // Record maximum number of ls.
      }
      else Nentries[k]=0;
    }
  }

  // CASE 2 -- one Cl table:
  else {
    CountEntries(prefix, &Nlinput, &ncols);
    NinputCls = GetColumnNames(prefix, &filelist) - 1; // In this case, NinputCls is determined by the input Cls available.
    cout << "Found " << NinputCls << " Cls in file " << prefix << ":" << endl;
    if (NinputCls+1 != ncols) error("ClProcess: input Cl file has different number of columns and column names.");
  }

  // Allocate memory to store C(l)s:
  // First two indexes are CovMatrix indexes and last is for ll.
  // fnz stores the order that the fields are stored in CovMatrix.
  fnz     =     matrix<int>(0, Nfields-1, 0, 1);                 // Records what field is stored in each element of CovMatrix.
  fnzSet  =    vector<bool>(0, Nfields-1);                       // For bookkeeping.
  ll      = tensor3<double>(0, Nfields-1, 0, Nfields-1, 0, Nlinput); // Records the ll for each C(l) file. 
  Cov     = tensor3<double>(0, Nfields-1, 0, Nfields-1, 0, Nlinput); // Records the C(l) for each C(l) file.
  IsSet   =    matrix<bool>(0, Nfields-1, 0, Nfields-1);           // For bookkeeping.
  NentMat =     matrix<int>(0, Nfields-1, 0, Nfields-1);           // Number of C(l) entries in file.
  for(i=0; i<Nfields; i++) for(j=0; j<Nfields; j++) IsSet[i][j]=0;
  for(i=0; i<Nfields; i++) fnzSet[i]=0;
  

  // Internal check that Field assignment in cov. matrices is working properly: 
  for (k=0; k<Nfields*Nfields; k++) {
    i = k/Nfields;      j = k%Nfields;
    fieldlist.Index2Name(i, &af, &az);
    fieldlist.Index2Name(j, &bf, &bz);
    // Record the order of the fields in CovMatrix:  
    if (fnzSet[i]==0) { fnz[i][0] = af; fnz[i][1] = az; fnzSet[i] = 1; }
    else if (fnz[i][0] != af || fnz[i][1] != az) error("ClProcess: field order in CovMatrix is messed up!"); 
    if (fnzSet[j]==0) { fnz[j][0] = bf; fnz[j][1] = bz; fnzSet[j] = 1; }
    else if (fnz[j][0] != bf || fnz[j][1] != bz) error("ClProcess: field order in CovMatrix is messed up!");
  }
  

  // CASE 1 -- Load Cls from individual files and stores in Cov structure:
  if (IsPrefix==true) {
    wrapper = vector<double*>(0,1);
    // LOOP over all possible input Cls:
    for (k=0; k<NinputCls; k++) {
      i = k/Nfields;      j = k%Nfields;
      if (filelist[k].size()>0) {
	// Import data:
	cout << filelist[k] << " goes to ["<<i<<", "<<j<<"]" << endl;
	wrapper[0] = &(ll[i][j][0]);
	wrapper[1] = &(Cov[i][j][0]);
	ImportVecs(wrapper, Nentries[k], 2, filelist[k]);
	NentMat[i][j] = Nentries[k];
	IsSet[i][j]=1;
      }
    }
    // Records Cl order:
    fieldlist.RecordInputClOrder(filelist, NinputCls);
    // Free auxiliary memory:
    free_vector(Nentries, 0, NinputCls-1);
    free_vector(wrapper,  0, 1);
  }

  // CASE 2 -- Loads Cls from a single Cl table and stores in Cov structure:
  else {
    // Assign wrapper slots to input table:
    wrapper    = vector<double*>(0,NinputCls);
    wrapper[0] = vector<double> (0,Nlinput);  // First column in Cl input table must be L.
    dump       = vector<double> (0,Nlinput);  // Memory slots to dump unwanted Cls.
    // LOOP over provided Cls:
    for (k=1; k<1+NinputCls; k++) {
      fieldlist.String2NamePair(filelist[k], &af, &az, &bf, &bz);
      i = fieldlist.Name2Index(af,az,NULL,0); // If Cl is not in FIELDS_INFO file, i=-1.
      j = fieldlist.Name2Index(bf,bz,NULL,0); // If Cl is not in FIELDS_INFO file, j=-1.
      if (i!=-1 && j!=-1) {
	// Assign a Cov entry to a wrapper slot:
	cout << filelist[k] << " goes to ["<<i<<", "<<j<<"]" << endl;
	wrapper[k]=&(Cov[i][j][0]);
	NentMat[i][j] = Nlinput;
	IsSet[i][j]=1;
      }
      else wrapper[k] = dump;
    }
    // Loads Cl table into wrapper (that actually points to l vector and Cov entries):
    ImportVecs(wrapper, Nlinput, 1+NinputCls, prefix);
    free_vector(dump, 0,Nlinput);
    // Copy l column to the Cov entry:
    for (k=1; k<1+NinputCls; k++) {
      fieldlist.String2NamePair(filelist[k], &af, &az, &bf, &bz);
      i = fieldlist.Name2Index(af,az,NULL,0); // If Cl is not in FIELDS_INFO file, i=-1.
      j = fieldlist.Name2Index(bf,bz,NULL,0); // If Cl is not in FIELDS_INFO file, j=-1.
      if (i!=-1 && j!=-1) for (l=0; l<Nlinput; l++) ll[i][j][l] = wrapper[0][l];
    }
    // Records Cl order (skip first entry which should be L):
    fieldlist.RecordInputClOrder(&(filelist[1]), NinputCls);
    // Deallocate auxiliary memory:
    free_vector(wrapper[0], 0, Nlinput);
    free_vector(wrapper,    0, NinputCls);    
  }
  if (config.readi("ALLOW_MISS_CL")==1) cout << "ALLOW_MISS_CL=1: will set totally missing Cl's to zero.\n";
  
  
  // Check if every field was assigned a position in the CovMatrix:
  for (i=0; i<Nfields; i++) if (fnzSet[i]==0) error("ClProcess: some position in CovMatrix is unclaimed.");
  free_vector(fnzSet, 0, Nfields-1);
  // If positions are OK and output required, print them out:
  if (config.reads("FLIST_OUT")!="0") {
    outfile.open(config.reads("FLIST_OUT").c_str());
    if (!outfile.is_open()) error("ClProcess: cannot open FLIST_OUT file.");
    PrintTable(fnz, Nfields, 2, &outfile);
    outfile.close();
    cout << ">> Written field list to "+config.reads("FLIST_OUT")<<endl;
  }
  free_matrix(fnz, 0, Nfields-1, 0,1);

  // Exit if this is the last output requested:
  if (ExitAt=="FLIST_OUT") return 1;  
  

  /*************************************************************/
  /*** PART 1.5: Apply various window functions if requested ***/
  /*************************************************************/
  double WinFuncVar, *pixwin, *pixell, lsup, supindex, factor;
  Spline pixSpline;

  // Re-scale all Cls by a constant factor:
  factor = config.readd("SCALE_CLS"); 
  if (factor != 1.0) {
    Announce("Re-scaling all C(l)s by SCALE_CLS...");
    // LOOP over existing C(l)s:
#pragma omp parallel for schedule(dynamic) private(i, j)
    for (k=0; k<Nfields*Nfields; k++) {
      i=k/Nfields;  j=k%Nfields;
      // In-place C(l) re-scaling:
      if (IsSet[i][j]==1) ScaleCls(Cov[i][j], factor, Cov[i][j], NentMat[i][j]);
    } // End over LOOP over existing C(l)s.
    Announce();
  } // End of IF Re-scaling requested.


  // Gaussian beam:
  WinFuncVar = config.readd("WINFUNC_SIGMA");            // WINFUNC_SIGMA will be transformed to radians and squared below. 
  if (WinFuncVar > 0.0) {
    Announce("Applying Gaussian window function to C(l)s... ");
    WinFuncVar = WinFuncVar/60.0*M_PI/180.0;             // From arcmin to radians.
    WinFuncVar = WinFuncVar*WinFuncVar;                  // From std. dev. to variance.
    // LOOP over existing C(l)s:
#pragma omp parallel for schedule(dynamic) private(i, j)
    for (k=0; k<Nfields*Nfields; k++) {
      i=k/Nfields;  j=k%Nfields;
      if (IsSet[i][j]==1) {
	// In-place C(l) change due to Gaussian window function:
	ApplyGausWinFunc(Cov[i][j], WinFuncVar, ll[i][j], Cov[i][j], NentMat[i][j]);
      }
    } // End over LOOP over existing C(l)s.
    Announce();
  } // End of IF Smoothing requested.


  // Healpix pixel window function:      
  if (config.readi("APPLY_PIXWIN")==1) {
    Announce("Applying Healpix pixel window function to C(l)s... ");
    // Prepare spline of the window function [input C(l)s might be at random ell]:
    m      = config.readi("NSIDE");
    pixell = vector<double>(0, 4*m);
    pixwin = vector<double>(0, 4*m);
    status = ReadHealpixData(1, config, pixwin, 2);
    if (status!=0) error("ClProcess: cannot read Healpix pixel window FITS.");
    for (i=0; i<=4*m; i++) {
      pixell[i] = (double)i;
      pixwin[i] = pixwin[i]*pixwin[i];
    }
    pixSpline.init(pixell, pixwin, 4*m+1);
    free_vector(pixell, 0, 4*m);
    free_vector(pixwin, 0, 4*m);
    // LOOP over existing C(l)s:
#pragma omp parallel for schedule(dynamic) private(i, j, l)
    for (k=0; k<Nfields*Nfields; k++) {
      i=k/Nfields;  j=k%Nfields;
      if (IsSet[i][j]==1) {
	// In-place C(l) change due to pixel window function:
	if(ll[i][j][NentMat[i][j]-1] > 4*m) warning("ClProcess: input C(l) overshoot Healpix pixel window function.");
	for(l=0; l<NentMat[i][j]; l++) Cov[i][j][l] = pixSpline(ll[i][j][l])*Cov[i][j][l];
      }
    } // End over LOOP over existing C(l)s.
    Announce();
  } // End of IF apply pixwin.


  // Exponential suppression:
  lsup     = config.readd("SUPPRESS_L");
  supindex = config.readd("SUP_INDEX");
  if (lsup >= 0.0 && supindex >= 0.0) {
    Announce("Applying exponential suppression to C(l)s... ");
    // LOOP over existing C(l)s:
#pragma omp parallel for schedule(dynamic) private(i, j, l)
    for (k=0; k<Nfields*Nfields; k++) {
      i=k/Nfields;  j=k%Nfields;
      if (IsSet[i][j]==1) {
	// In-place C(l) change due to Gaussian window function:
	for(l=0; l<NentMat[i][j]; l++) Cov[i][j][l] = Cov[i][j][l]*suppress(ll[i][j][l], lsup, supindex);
      }
    } // End over LOOP over existing C(l)s.
    Announce();
  } // End of IF exponential suppression requested.


  // Print C(l)s to files if requested:
  filename = config.reads("SMOOTH_CL_PREFIX");
  if (filename!="0") {
    // Output one Cl per file, following the input Cls:
    if (filename.length()>=4 && filename.substr(filename.length()-4,4)!=".dat") {
      for(i=0; i<Nfields; i++) for(j=0; j<Nfields; j++) if (IsSet[i][j]==1) {
	    PrintOut(filename, i, j, fieldlist, ll[i][j], Cov[i][j], NentMat[i][j]);
	  }
      cout << ">> Smoothed C(l)s written to prefix "+filename<<endl;
    }
    // Place all Cls in one table, following the input Cls order:
    else {
      if (IsPrefix==true) error("ClProcess: prefix CL_PREFIX to single file (.dat) SMOOTH_CL_PREFIX is currently not implemented.");
      auxMatrix = matrix<double>(0,Nlinput, 0,NinputCls);
      for (i=0; i<=Nlinput; i++) for (j=0; j<=NinputCls; j++) auxMatrix[i][j]=0; 
      VecInColumn(ll[0][0], auxMatrix, 0, 1+NinputCls, Nlinput);
      for(i=0; i<Nfields; i++) for(j=0; j<Nfields; j++) if (IsSet[i][j]==1) {
	    k = fieldlist.GetInputClOrder(i,j);
	    VecInColumn(Cov[i][j], auxMatrix, 1+k, 1+NinputCls, Nlinput);
	  }
      // Output Cls table to file:
      outfile.open(filename.c_str());
      if (!outfile.is_open()) error("ClProcess: cannot open SMOOTH_CL_PREFIX file.");
      PrintHeader(filelist, 1+NinputCls, &outfile);
      PrintTable(auxMatrix, Nlinput, 1+NinputCls, &outfile);
      free_matrix(auxMatrix, 0,Nlinput, 0,NinputCls);
      outfile.close();
      cout << ">> Smoothed C(l)s written to file "+filename<<endl;
    }
  }
  if (ExitAt=="SMOOTH_CL_PREFIX") return 1;


  /*** Continue organizing C(l)s ***/

  // Look for the maximum l value described by all C(l)s:
  for(i=0; i<Nfields; i++) for(j=0; j<Nfields; j++) if (IsSet[i][j]==1) {
	if (ll[i][j][NentMat[i][j]-1]>HWMAXL) error ("ClProcess: too high l in C(l)s: increase HWMAXL.");
	if (ll[i][j][NentMat[i][j]-1]<lastl) lastl = (int)ll[i][j][NentMat[i][j]-1];
      }
  cout << "Maximum l in input C(l)s:    "<<lastl<<endl;
  // lmax cannot be larger than the last ell provided as input:
  if (lmax>lastl) error("ClProcess: C(l)s provided are not specified up to requested LRANGE maximum.");\

  // If requested, truncate C(l)s to lmax given in LRANGE (will only use up to this multipole):
  if (config.readi("CROP_CL")==1) lastl=lmax;
  else if (config.readi("CROP_CL")!=0) warning("ClProcess: unknown CROP_CL option, will assume CROP_CL=0.");
  cout << "Maximum l in transformation: "<<lastl<<endl;
  Nls=lastl+1; // l=0 is needed for DLT. Nls is known as 'bandwidth' (bw) in s2kit 1.0 code.
  (*NlsOut)=Nls;
    
  // Allocate gsl_matrices that will receive covariance matrices for each l.
  Announce("Allocating data-cube needed for Cholesky decomposition... ");
  CovByl = GSLMatrixArray(Nls, Nfields, Nfields);
  (*CovBylAddr) = CovByl;
  Announce();
 

  /*****************************************************************/
  /*** PART 2: Compute auxiliary gaussian C(l)s if LOGNORMAL     ***/
  /*****************************************************************/
  double *tempCl, *LegendreP, *workspace, *xi, *theta, *DLTweights, *lls;

  if (dist==lognormal) {
    cout << "LOGNORMAL realizations: will compute auxiliary gaussian C(l)s:\n";
    // Loads necessary memory:
    Announce("Allocating memory for DLT... ");
    workspace  = vector<double>(0, 16*Nls-1);
    LegendreP  = vector<double>(0, 2*Nls*Nls-1);
    DLTweights = vector<double>(0, 4*Nls-1);
    Announce();
    
    // Load s2kit 1.0 Legendre Polynomials:
    Announce("Generating table of Legendre polynomials... ");
    PmlTableGen(Nls, 0, LegendreP, workspace);
    free_vector(workspace, 0, 16*Nls-1);
    Announce();
    // Compute s2kit 1.0 Discrete Legendre Transform weights:
    Announce("Calculating forward DLT weights... ");
    makeweights(Nls, DLTweights);
    Announce();
  }

  // Vector of ells is only necessary for output:
  if (config.reads("GCLOUT_PREFIX")!="0" || config.reads("REG_CL_PREFIX")!="0") {
    Announce("Generating list of ells... ");
    lls = vector<double>(0, lastl);
    for (i=0; i<=lastl; i++) lls[i]=(double)i;
    Announce();
  }
  // Angle theta is only necessary for output:
  if (config.reads("XIOUT_PREFIX")!="0" || config.reads("GXIOUT_PREFIX")!="0") {
    Announce("Generating table of sampling angles... ");
    theta    = vector<double>(0, 2*Nls-1);
    ArcCosEvalPts(2*Nls, theta);
    for (i=0; i<2*Nls; i++) theta[i] = theta[i]*180.0/M_PI;
    Announce();
  } 

  // LOOP over all C(l)s already set.
  if (dist==lognormal) Announce("Transforming C(l)s for the auxiliary Gaussian ones... ");
  else Announce("Interpolating C(l)s for all l's... ");
#pragma omp parallel for schedule(dynamic) private(tempCl, xi, workspace, filename, l, i, j)
  for (k=0; k<Nfields*Nfields; k++) {
    i=k/Nfields;  j=k%Nfields;
    if (IsSet[i][j]==1) {
      
      // Temporary memory allocation:
      tempCl    = vector<double>(0, lastl);
      xi        = vector<double>(0, 2*Nls-1);
      workspace = vector<double>(0, 2*Nls-1);

      // Interpolate C(l) for every l; input C(l) might not be like that:
      GetAllLs(ll[i][j], Cov[i][j], NentMat[i][j], tempCl, lastl, config.readi("EXTRAP_DIPOLE"));
	
      if (dist==lognormal) {              /** LOGNORMAL ONLY **/
	
	// Compute correlation function Xi(theta):
	ModCl4DLT(tempCl, lastl, -1, -1);
	Naive_SynthesizeX(tempCl, Nls, 0, xi, LegendreP);
	if (config.reads("XIOUT_PREFIX")!="0") { // Write it out if requested:
	  filename=PrintOut(config.reads("XIOUT_PREFIX"), i, j, fieldlist, theta, xi, 2*Nls);
	}

	// Transform Xi(theta) to auxiliary gaussian Xi(theta):
	status=GetGaussCorr(xi, xi, 2*Nls, fieldlist.mean(i), fieldlist.shift(i), fieldlist.mean(j), fieldlist.shift(j));
	if (status==EDOM) error("ClProcess: GetGaussCorr found bad log arguments.");
	if (i==j && xi[0]<0) warning("ClProcess: auxiliary field variance is negative.");
	if (config.reads("GXIOUT_PREFIX")!="0") { // Write it out if requested:
	  filename=PrintOut(config.reads("GXIOUT_PREFIX"), i, j, fieldlist, theta, xi, 2*Nls);
	}

	// Transform Xi(theta) back to C(l):
	Naive_AnalysisX(xi, Nls, 0, DLTweights, tempCl, LegendreP, workspace);
	ApplyClFactors(tempCl, Nls);
	if (config.reads("GCLOUT_PREFIX")!="0") { // Write it out if requested:
	  filename=PrintOut(config.reads("GCLOUT_PREFIX"), i, j, fieldlist, lls, tempCl, Nls);
	}	  
      }                                 /** END OF LOGNORMAL ONLY **/ 
	
      // Save auxiliary C(l):
      for (l=0; l<Nls; l++) CovByl[l]->data[i*Nfields+j]=tempCl[l];
	
      // Temporary memory deallocation:
      free_vector(tempCl, 0, lastl);
      free_vector(xi, 0, 2*Nls-1);
      free_vector(workspace, 0, 2*Nls-1);
    } // End of IF C(l)[i,j] is set.
  } // End of LOOP over C(l)[i,j] that were set.
  Announce();
  
  // Memory deallocation:
  free_tensor3(Cov,    0, Nfields-1, 0, Nfields-1, 0, Nlinput); 
  free_tensor3(ll,     0, Nfields-1, 0, Nfields-1, 0, Nlinput); 
  free_matrix(NentMat, 0, Nfields-1, 0, Nfields-1);
  if (config.reads("XIOUT_PREFIX")!="0" || config.reads("GXIOUT_PREFIX")!="0") free_vector(theta, 0, 2*Nls-1);

  // Output information:
  if (config.reads("XIOUT_PREFIX")!="0") 
    cout << ">> Correlation functions written to prefix "+config.reads("XIOUT_PREFIX")<<endl;
  if (config.reads("GXIOUT_PREFIX")!="0") 
    cout << ">> Associated Gaussian correlation functions written to prefix "+config.reads("GXIOUT_PREFIX")<<endl;
  if (config.reads("GCLOUT_PREFIX")!="0") 
    cout << ">> C(l)s for auxiliary Gaussian variables written to prefix "+config.reads("GCLOUT_PREFIX")<<endl;
  // Exit if this is the last output requested:
  if (ExitAt=="XIOUT_PREFIX"  || 
      ExitAt=="GXIOUT_PREFIX" || 
      ExitAt=="GCLOUT_PREFIX") return 1;
    
  // Set Cov(l)[i,j] = Cov(l)[j,i]
  Announce("Set remaining cov. matrices elements based on symmetry... ");
  k = config.readi("ALLOW_MISS_CL");
  if (k!=1 && k!=0) error("ClProcess: unknown option for ALLOW_MISS_CL.");
  for(i=0; i<Nfields; i++)
    for(j=0; j<Nfields; j++)
      // Look for empty entries in Cov:
      if (IsSet[i][j]==0) {
	// If transpose is empty too:
	if (IsSet[j][i]==0) {
	  // Set transpose to zero if this is allowed:
	  if (k==1) { for (l=0; l<Nls; l++) CovByl[l]->data[j*Nfields+i] = 0.0; IsSet[j][i]=1; }
	  // If not allowed, return error:
	  else { sprintf(message,"ClProcess: [%d, %d] could not be set because [%d, %d] was not set.",i,j,j,i); error(message); }
	}
	for (l=0; l<Nls; l++) CovByl[l]->data[i*Nfields+j] = CovByl[l]->data[j*Nfields+i];
	IsSet[i][j] = 1;
      }
  for(i=0; i<Nfields; i++) for(j=0; j<Nfields; j++) if (IsSet[i][j]!=1) {
	sprintf(message,"ClProcess: [%d, %d] was not set.",i,j);
	error(message);
      }
  Announce();
  free_matrix(IsSet, 0, Nfields-1, 0, Nfields-1);

  // Output covariance matrices for each l if requested:
  GeneralOutput(CovByl, config, "COVL_PREFIX", 0);
  if (config.reads("COVL_PREFIX")!="0") 
    cout << ">> Cov. matrices written to prefix "+config.reads("COVL_PREFIX")<<endl;
  // Exit if this is the last output requested:
  if (ExitAt=="COVL_PREFIX") return 1;
  
  // Verify basic properties of auxiliary cov. matrices:
  Announce("Verifying aux. Cov. matrices properties... ");
  badcorrfrac = config.readd("BADCORR_FRAC");
  mindiagfrac = config.readd("MINDIAG_FRAC");
 
  // If minimum value for Field variances is set, find the current minimum value:
  if (mindiagfrac > 0.0) {
    mindiag = LARGESTVARIANCE;
    for (l=lmin; l<=lmax; l++) 
      for (i=0; i<Nfields; i++) {
	temp = CovByl[l]->data[i*Nfields+i];
	if (temp > 0.0 && temp < mindiag) mindiag = temp;
      }
  }
  for (l=lmin; l<=lmax; l++) // Skipping l=0 since matrix should be zero.
    for (i=0; i<Nfields; i++) {
      // Verify that diagonal elements are non-negative:
      if (CovByl[l]->data[i*Nfields+i]<0.0) {	
	sprintf(message, "ClProcess: Cov. matrix (l=%d) element [%d, %d] is negative.", l, i, i);
	warning(message);
      }
      // Verify that (or set to) diagonal values are non-zero: 
      if (CovByl[l]->data[i*Nfields+i]==0.0) {
	if (mindiagfrac > 0.0) CovByl[l]->data[i*Nfields+i] = mindiagfrac * mindiag;
	else {
	  sprintf(message, "ClProcess: Cov. matrix (l=%d) element [%d, %d] is zero.", l, i, i);
	  warning(message);
	}
      }
      for (j=i+1; j<Nfields; j++) {
	// Correlations c should be limited to -1<c<1.
	temp = CovByl[l]->data[i*Nfields+j]/sqrt(CovByl[l]->data[i*Nfields+i]*CovByl[l]->data[j*Nfields+j]);
	if (temp>1.0 || temp<-1.0) {
	  // Try increasing variances if correlation is absurd:
	  cout << "  Aux. Cov. matrix (l="<<l<<") element ["<<i<<", "<<j<<"] results in correlation "<<temp
	       <<". Fudging variances with BADCORR_FRAC...\n";
	  CovByl[l]->data[i*Nfields+i] *= (1.0+badcorrfrac);
	  CovByl[l]->data[j*Nfields+j] *= (1.0+badcorrfrac);
	  temp = CovByl[l]->data[i*Nfields+j]/sqrt(CovByl[l]->data[i*Nfields+i]*CovByl[l]->data[j*Nfields+j]);
	  if (temp>1.0 || temp<-1.0) warning("ClProcess: BADCORR_FRAC could not solve the issue.");
	}
      }
    }      
  Announce();


  /****************************************************************************/
  /*** PART 3: Regularize (make them positive definite) covariance matrices ***/
  /****************************************************************************/
  gsl_matrix *gslm;
  double *MaxChange, MMax;
  int lMMax, lstart, lend, FailReg=0, NCls;

  // If producing the regularized lognormal C(l)s, all ls must be regularized (skip l=0 because is set to zero).
  // Else, only the cov. matrices for requested ls are regularized.
  // Note that a very high exponential suppression of C(l)s make difficult to regularize matrices.
  if (dist==lognormal && config.reads("REG_CL_PREFIX")!="0") { lstart = 1;  lend = Nls-1; }
  else { lstart = lmin;  lend = lmax; }
  MaxChange = vector<double>(lstart, lend);
  
  Announce("Regularizing cov. matrices... ");
  
#pragma omp parallel for schedule(dynamic) private(gslm, filename)  
  for (l=lstart; l<=lend; l++) {

    // Check pos. defness, regularize if necessary, keep track of changes:
    gslm = gsl_matrix_alloc(Nfields, Nfields);
    gsl_matrix_memcpy(gslm, CovByl[l]);
    status       = RegularizeCov(CovByl[l], config);
    MaxChange[l] =   MaxFracDiff(CovByl[l], gslm);
    if (status==9) { 
      sprintf(message, "ClProcess: RegularizeCov for l=%d reached REG_MAXSTEPS with Max. change of %g.",l,MaxChange[l]); 
      warning(message);
      FailReg=1;
    }
    gsl_matrix_free(gslm);
    // Output regularized matrix if requested:
    if (config.reads("REG_COVL_PREFIX")!="0") {
      filename=config.reads("REG_COVL_PREFIX")+"l"+ZeroPad(l,lend)+".dat";
      GeneralOutput(CovByl[l], filename, 0);
    }
  }
  Announce();
  if (FailReg==1) error("ClProcess: failed to regularize covariance matrices.");
  
  // Dump changes in cov. matrices to the screen:
  MMax  = 0.0; lMMax = 0;
  for (l=lmin; l<=lmax; l++) if (MaxChange[l]>MMax) {MMax = MaxChange[l]; lMMax = l;}
  cout << "Max. frac. change for "<<lmin<<"<=l<="<<lmax<<" at l="<<lMMax<<": "<<MMax<<endl;  
  free_vector(MaxChange, lstart, lend);
  // Output regularized matrices if requested:
  if (config.reads("REG_COVL_PREFIX")!="0") 
    cout << ">> Regularized cov. matrices written to prefix "+config.reads("REG_COVL_PREFIX")<<endl;
  // Exit if this is the last output requested:
  if (ExitAt=="REG_COVL_PREFIX") return 1;

  /***********************************************************/
  /*** PART 4: Obtain regularized input Cls if requested   ***/
  /***********************************************************/
  std::string *header;

  prefix = config.reads("REG_CL_PREFIX");
  if (prefix!="0") {
    if (dist==lognormal) Announce("Computing regularized lognormal Cls... ");
    if (dist==gaussian)  Announce("Computing regularized Gaussian Cls... ");
    NCls = (Nfields*(Nfields+1))/2;
    if (prefix.length()>=4 && prefix.substr(prefix.length()-4,4)==".dat") {
      header = vector<std::string>(0, NCls);
      header[0].assign("l");
      auxMatrix  = matrix<double>(0,lastl, 0,NCls);
      for (i=0; i<=lastl; i++) for (j=0; j<=NCls; j++) auxMatrix[i][j]=0;
    }
    // LOOP over fields:
#pragma omp parallel for schedule(dynamic) private(tempCl, xi, workspace, filename, l, m, i, j, n)
    for (k=0; k<NCls; k++) {
      l = (int)((sqrt(8.0*(NCls-1-k)+1.0)-1.0)/2.0);
      m = NCls-1-k-(l*(l+1))/2;
      i = Nfields-1-l;
      j = Nfields-1-m;

      // Copy the Cl to a vector:
      tempCl = vector<double>(0, lastl);
      for (l=0; l<Nls; l++) tempCl[l] = CovByl[l]->data[i*Nfields+j];
	
      if (dist==lognormal) {
	// Temporary memory allocation:
	xi        = vector<double>(0, 2*Nls-1);
	workspace = vector<double>(0, 2*Nls-1);
	// Compute correlation function Xi(theta):
	ModCl4DLT(tempCl, lastl, -1, -1); // Suppression not needed (it was already suppressed).
	Naive_SynthesizeX(tempCl, Nls, 0, xi, LegendreP);
	// Get Xi(theta) for lognormal variables:
	GetLNCorr(xi, xi, 2*Nls, fieldlist.mean(i), fieldlist.shift(i), fieldlist.mean(j), fieldlist.shift(j));
	// Compute the Cls:
	Naive_AnalysisX(xi, Nls, 0, DLTweights, tempCl, LegendreP, workspace);
	ApplyClFactors(tempCl, Nls, -1, -1);
	// Temporary memory deallocation:
	free_vector(xi, 0, 2*Nls-1);
	free_vector(workspace, 0, 2*Nls-1);
      }
      
      // Output regularized Cls or prepare for output:
      if (prefix.length()>=4 && prefix.substr(prefix.length()-4,4)!=".dat") {
	filename=PrintOut(prefix, i, j, fieldlist, lls, tempCl, Nls);
      }
      else {
	header[1+k] = Fields2Label(i, j, fieldlist);
	//VecInColumn(lls,    auxMatrix,   0, 1+NCls, Nls);
	//n = fieldlist.GetInputClOrder(i,j);
	//VecInColumn(tempCl, auxMatrix, 1+n, 1+NCls, Nls);
	VecInColumn(tempCl, auxMatrix, 1+k, 1+NCls, Nls);
      }

      free_vector(tempCl, 0, lastl);
    } // End of LOOP over fields. 
    Announce();

    if (prefix.length()>=4 && prefix.substr(prefix.length()-4,4)!=".dat") 
      cout << ">> Regularized C(l)s written to prefix "+config.reads("REG_CL_PREFIX")<<endl;
    else {
      // Output Cls table to file:
      //if (IsPrefix==true) error("ClProcess: prefix CL_PREFIX to single file (.dat) REG_CL_PREFIX is currently not implemented.");
      outfile.open(prefix.c_str());
      if (!outfile.is_open()) error("ClProcess: cannot open REG_CL_PREFIX file.");
      PrintHeader(header, 1+NCls, &outfile);
      VecInColumn(lls, auxMatrix, 0, 1+NCls, Nls);
      PrintTable(auxMatrix, Nls, 1+NCls, &outfile);
      free_matrix(auxMatrix, 0,lastl, 0,NCls);
      free_vector(header, 0, NCls);
      outfile.close();
      cout << ">> Regularized C(l)s written to file "+config.reads("REG_CL_PREFIX")<<endl;
    }

  } // End of computing regularized lognormal Cls.
  

  // Freeing memory: from now on we only need CovByl, means, shifts.
  if (config.reads("GCLOUT_PREFIX")!="0" || config.reads("REG_CL_PREFIX")!="0") free_vector(lls, 0, lastl);
  if (dist==lognormal) {
    Announce("DLT memory deallocation... ");
    free_vector(LegendreP, 0, 2*Nls*Nls-1);
    free_vector(DLTweights, 0, 4*Nls-1); 
    Announce();
  }

  // Exit if this is the last output requested:
  if (ExitAt=="REG_CL_PREFIX") return 1;


  free_vector(filelist, 0, NinputCls-1);
  return 0; // Any return in the middle of this function returns 1.
}
