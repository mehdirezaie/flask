# you need to set LD_LIBRARY_PATH appropriately, e.g.,
# export LD_LIBRARY_PATH=/home/mehdi/local/lib64:${LD_LIBRARY_PATH}
# where you have healpix_cxx, gsl, fitsio, and sharp library files
ver=v1
tag=$1

for iseed in {1..1000};
do
  clpref=/home/mehdi/github/flask/data/desiCl${tag}-
  MAPOUT=/home/mehdi/data/lognormal/${ver}/lrg-${tag}-${iseed}-
  #CATOUT=/home/mehdi/data/lognormal/${ver}/lrg-cat-${iseed}.fits
  
  DIR="$(dirname "${MAPOUT}")"
  echo $MAPOUT
  echo "[${DIR}]"
  du -h ${clpref}f1z1f1z1.dat
  
  if [ ! -d $DIR ]
  then
    echo $DIR "does not exist"
    mkdir -p $DIR
  fi
  #CATALOG_OUT: $CATOUT
  bin/flask desi.config CL_PREFIX: $clpref RNDSEED: $iseed MAPWERFITS_PREFIX: $MAPOUT
done

