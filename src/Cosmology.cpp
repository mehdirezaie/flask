#include "Cosmology.hpp"
#include "Utilities.hpp"
#include "Integral.hpp"
#include "interpol.hpp"
#include "ParameterList.hpp"
#include <math.h>

/*** Methods of the Cosmology class ***/

Cosmology::Cosmology() {
  curv=1;
  //curv=0;       // Cosmological distances in ComDist function are calculated assuming flat universe.
  lowz=1;       // Cosmological distances in ComDist function are calculated ignoring radiation contributions.
  c=299792.458; /* km/s     */
  H100=100;     /* km/s/Mpc */
  NOTSETd=-666;
  NOTSETi=-666; 

  Om      = NOTSETd;    
  Ode     = NOTSETd;
  Ob      = NOTSETd;
  Onu     = NOTSETd;
  Omh2    = NOTSETd;    
  Odeh2   = NOTSETd;
  Obh2    = NOTSETd;
  Onuh2   = NOTSETd;
  Ok      = NOTSETd;
  wde     = NOTSETd;
  h       = NOTSETd;
  Nnu     = NOTSETi;
  deltaH2 = NOTSETd;
  ns      = NOTSETd;
  galdens = NOTSETd;
}

void Cosmology::load(const ParameterList & config) {
  using namespace ParDef;
  Om      = config.readd("OMEGA_m");
  Ode     = config.readd("OMEGA_L");
  wde     = config.readd("W_de");
  galdens = config.readd("GALDENSITY");
  SetOther();
}

void Cosmology::SetOther() {
  if (wde==NOTSETd)     error("Cosmology::SetOther: equation of state wde must be set.");
  // Set Omega_curvature:
  if (Om!=NOTSETd && Ode!=NOTSETd) Ok = 1.0-Om-Ode;
  else error("Cosmology::SetOther: cannot set curvature due to missing Om and/or Ode.");
  // Check spatial geometry:
  if (fabs(Om+Ode-1.0)>0.0001 && curv==0) {
    char message[100];
    sprintf(message, "Cosmology::SetOther: ComDist assumes flat universe but Om+Ode= %g.", Om+Ode);
    warning(message);
  }
}


/*** Separate cosmological functions ***/


// Sub-functions of ComDist and dChidz.
double Eh(const Cosmology & p, double z) {
  return sqrt(p.Om*pow(1+z,3) + p.Ok*pow(1+z,2) + p.Ode*pow(1+z,3*(1+p.wde)));
}
double ComDistIntegrand(const Cosmology & p, double z) {
  return 1.0/Eh(p, z);
}

// Returns the radial comoving distance along the line of sight in h^-1 Mpc:
double ComDist(const Cosmology & p, double z) {
  char message[200];
  const int ngrid=500;
  const double zmin=0.0, zmax=8.0, maxerr=0.1;
  static bool init=0;
  static double zgrid[ngrid+1], dgrid[ngrid+1];
  int i;
  double dist;

  // Check for errors:
  if (z>zmax) { sprintf(message,"ComDist: z=%g is beyond hard-coded zmax=%g",z,zmax); error(message); }
  if (z<zmin) { sprintf(message,"ComDist: z=%g is beyond hard-coded zmin=%g",z,zmin); error(message); }

  // Initialize:
  if (init==0) {
    for (i=0; i<=ngrid; i++) {
      zgrid[i] = zmin + i*((zmax-zmin)/ngrid);
      dgrid[i] = p.c/p.H100*qromb(ComDistIntegrand, 0.0, zgrid[i], p);
    }
    init=1;
  }
  // Return from interpolation:
  dist=Interpol(zgrid, ngrid+1, dgrid, z);
  return dist;  
}


// Transverse comoving distance in n^-1 Mpc, Chi is the radial comoving distance:
double TransverseDist (const Cosmology & p, double Chi) {
  double curvFactor = p.c / p.H100 / sqrt(p.Ok);

  if     (p.Ok == 0.0) return Chi;
  else if (p.Ok > 0.0) return curvFactor * sinh(Chi/curvFactor);
  else if (p.Ok < 0.0) return curvFactor * sin(Chi/curvFactor);
  
  error("TransverseDist: no return options left, this is crazy");
}


// Derivative of Radial Comoving distance with respect to redshift:
double dChidz(const Cosmology & p, double z) {
  return p.c/p.H100*ComDistIntegrand(p, z);
}



// Weak lensing (convergence kappa) weights (kernel) when line-of-sight integrating contrast density in redshift:
double KappaWeightByZ(const Cosmology & p, double z, double zsource) {
  
  return 3.0/2.0*p.H100*p.H100/p.c/p.c*p.Om * (1+z) 
    * TransverseDist(p,ComDist(p,z)) * TransverseDist(p,ComDist(p,zsource)-ComDist(p,z)) 
    / TransverseDist(p,ComDist(p,zsource)) * dChidz(p, z); 
}


double AvgKappaWeightByZ(const Cosmology & p, double zmin, double zmax, double zsource) {
  return qromb5(KappaWeightByZ, zmin, zmax, zsource, p)/(zmax-zmin);
}
