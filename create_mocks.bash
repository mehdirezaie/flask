
ver=v0

for iseed in {1..1000};
do
  MAPOUT=/home/mehdi/data/lognormal/${ver}/lrg-${iseed}-
  CATOUT=/home/mehdi/data/lognormal/${ver}/lrg-cat-${iseed}.fits
  
  #CATALOG_OUT: $CATOUT
  bin/flask desi.config RNDSEED: $iseed MAPWERFITS_PREFIX: $MAPOUT
done

