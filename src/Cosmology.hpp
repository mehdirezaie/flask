#ifndef COSMOLOGY_H
#define COSMOLOGY_H 1

#include <iostream>
#include "ParameterList.hpp"

class Cosmology {
private:
  char curv;      // -1 Open; 0 Flat; +1 Closed.
  bool lowz;      // Confirm that low-z approximations can be used.
  double NOTSETd; // double identifier for parameteres not set.
  int NOTSETi;    // int identifier for parameters not set.
public:
  double Om;      // Total matter (DM + barions + massive neutrinos) density parameter.
  double Ode;     // Dark energy density parameter.
  double Ob;      // Barion density parameter.
  double Onu;     // Massive neutrino density parameter.
  double Omh2;    // Total matter (DM + barions + massive neutrinos) density parameter.
  double Odeh2;   // Dark energy density parameter times h^2.
  double Obh2;    // Barion density parameter times h^2.
  double Onuh2;   // Massive neutrino density parameter times h^2.
  double Ok;      // Curvature density parameter.
  double wde;     // Dark energy equation of state
  double h;       // Hubble constant divided by 100km/s/Mpc.
  int Nnu;        // Number of massive species of neutrinos.
  double H100;    // 100 km/s/Mpc.
  double c;       // Speed of light in km/s.
  double deltaH2; // Power spectrum normalization.
  double ns;      // Power spectrum spectral index.
  double galdens; // 3D comoving galaxy density in (h^-1 Mpc)^3.
  Cosmology();
  void load(const ParameterList & config);
  void SetOther();
};

double Eh(const Cosmology & p, double z);
double ComDist(const Cosmology & p, double z);
double KappaWeightByZ(const Cosmology & p, double z, double zsource);
double AvgKappaWeightByZ(const Cosmology & p, double zmin, double zmax, double zsource);

#endif
