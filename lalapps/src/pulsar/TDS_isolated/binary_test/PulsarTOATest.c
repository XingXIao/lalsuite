/* Matt Pitkin 18/10/12
   Code to check TOA files (generated by TEMPO2) against TOAs calculated from
   the given parameter file 
   
   Compile with:
   gcc PulsarTOATest.c -o PulsarTOATest -L${LSCSOFT_LOCATION}/lib -I${LSCSOFT_LOCATION}/include -lm -llalsupport -llal -llalpulsar -std=c99
   */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <getopt.h>

#include <lal/LALStdlib.h>
#include <lal/LALBarycenter.h>
#include <lal/LALInitBarycenter.h>
#include <lal/LALConstants.h>
#include <lal/Date.h>
#include <lal/LALString.h>

#include <lal/BinaryPulsarTiming.h>

#define USAGE \
"Usage: %s [options]\n\n"\
" --help (-h)              display this message\n"\
" --verbose (-v)           display all error messages\n"\
" --par-file (-p)          TEMPO2 parameter (.par) file\n"\
" --tim-file (-t)          TEMPO2 TOA (.tim) file\n"\
" --ephem (-e)             Ephemeris type (DE200 or [default] DE405)\n"\
" --clock (-c)             Clock correction file (default is none)\n"\
" --simulated (-s)         Set if the TOA file is from simulated data\n\
                          e.g. created with the TEMPO2 'fake' plugin:\n\
                          tempo2 -gr fake -f pulsar.par -ndobs 1 -nobsd 5\n\
                            -start 54832 -end 55562 -ha 8 -randha n -rms 0\n"\
"\n"

int verbose = 0;

typedef struct tagParams{
  char *parfile;
  char *timfile;
  char *ephem;
  char *clock;
  
  int simulated;
}Params;

void get_input_args(Params *pars, int argc, char *argv[]);

int main(int argc, char *argv[]){
  FILE *fpin=NULL;
  FILE *fpout=NULL;
  
  char filestr[256];
  double radioFreq=0.0, rf[10000];
  double TOA[10000];
  double num1;
  int telescope;
  int i=0, j=0, k=0;
  
  double PPTime[10000]; /* Pulsar proper time - corrected for solar system and
 binary orbit delay times */
  const double D = 2.41e-4; /* dispersion constant from TEMPO */
  
  /* Binary pulsar variables */
  BinaryPulsarParams params;
  BinaryPulsarInput input;
  BinaryPulsarOutput output;

  Params par;
  
  /* LAL barycentring variables */
  BarycenterInput baryinput;
  EarthState earth;
  EmissionTime emit;
  EphemerisData *edat=NULL;
  TimeCorrectionData *tdat=NULL;
  char *earthFile=NULL, *sunFile=NULL, *tcFile=NULL, *lalpath=NULL;

  double MJD_tcorr[10000];
  double tcorr[10000];

  double f0=0., f1=0., f2=0., f3=0., T=0.;
  double DM;
  
  long offset;
  
  TimeCorrectionType ttype;
  
  lalDebugLevel = 7;
  XLALSetErrorHandler( XLALExitErrorHandler );
  
  get_input_args(&par, argc, argv);
 
  if( verbose ){
    fprintf(stderr,"\n");
    fprintf(stderr,
"*******************************************************\n");
    fprintf(stderr, "** We are assuming that the TOAs where produced with \
**\n");
    fprintf(stderr, "** TEMPO2 and are sited at the Parkes telescope.     \
**\n");
    fprintf(stderr,
"*******************************************************\n");
  }
  if((fpin = fopen(par.timfile, "r")) == NULL){
    fprintf(stderr, "Error... can't open TOA file!\n");
    return 1;
  }

  /* read in TOA and phase info - assuming in the format output by TEMPO2 .tim
     file */
  while(!feof(fpin)){
    offset = ftell(fpin);
    char firstchar[256];
    
    /* if line starts with FORMAT, MODE, or a C or # then skip it */
    if( fscanf(fpin, "%s", firstchar) != 1 ) break;
    if( !strcmp(firstchar, "FORMAT") || !strcmp(firstchar, "MODE") ||
        firstchar[0] == '#' || firstchar[0] == 'C' ){
      if ( fscanf(fpin, "%*[^\n]") == EOF ) break;
      continue;
    }
    else{
      fseek(fpin, offset, SEEK_SET);
      
      /* is data is simulated with TEMPO2 fake plugin then it only has 5 columns */
      if (par.simulated){
        fscanf(fpin, "%s%lf%lf%lf%d", filestr, &radioFreq, &TOA[i],
&num1, &telescope);
      }
      else{
        char randstr1[256], randstr2[256], randstr3[256];
        int randnum;
        
        fscanf(fpin, "%s%lf%lf%lf%d%s%s%s%d", filestr, &radioFreq, &TOA[i],
&num1, &telescope, randstr1, randstr2, randstr3, &randnum);
      }
      rf[i] = radioFreq;
      
      i++;
    }
  }
  
  fclose(fpin);

  if( verbose ) fprintf(stderr, "I've read in the TOAs\n");

  /* read in telescope time corrections from file */
  if (par.clock != NULL){
    if( (fpin = fopen(par.clock, "r")) == NULL ){
      fprintf(stderr, "Error... can't open clock file for reading!\n");
      exit(1);
    }
   
    do{
      offset = ftell(fpin);
      char firstchar[256];
      
      /* if line starts with # then ignore as a comment */
      fscanf(fpin, "%s", firstchar);
      if( firstchar[0] == '#' ){
        if ( fscanf(fpin, "%*[^\n]") == EOF ) break;
        continue;
      }
      else{
        fseek(fpin, offset, SEEK_SET);
      
        fscanf(fpin, "%lf%lf", &MJD_tcorr[j], &tcorr[j]);
        j++;
      }
    }while(!feof(fpin));

    fclose(fpin);
  }

  /* read in binary params from par file */
  XLALReadTEMPOParFile(&params, par.parfile);
  
  if ( verbose ) fprintf(stderr, "I've read in the parameter file\n");

  /* set telescope location - TEMPO2 defaults to Parkes, so use this 
x,y,z components are got from tempo */
  if (telescope != 7){
    fprintf(stderr, "Error, TOA file not using the Parkes telescope!\n");
    exit(1);
  }
  
  /* location of the Parkes telescope */
  baryinput.site.location[0] = -4554231.5/LAL_C_SI;
  baryinput.site.location[1] = 2816759.1/LAL_C_SI;
  baryinput.site.location[2] = -3454036.3/LAL_C_SI;
  
  if((lalpath = getenv("LALPULSAR_PREFIX")) == NULL){
    fprintf(stderr, "LALPULSAR_PREFIX environment variable not set!\n");
    exit(1);
  }
  
  earthFile = XLALStringDuplicate(lalpath);
  sunFile = XLALStringDuplicate(lalpath);
  
  /* initialise the solar system ephemerides */
  if( par.ephem == NULL ){ /* default to DE405 */
    earthFile = XLALStringAppend( earthFile, 
                                  "/share/lalpulsar/earth00-19-DE405.dat.gz" );
    sunFile = XLALStringAppend( sunFile,
                                "/share/lalpulsar/sun00-19-DE405.dat.gz" );
  }
  else if( strcmp(par.ephem, "DE200") == 0 ){
    earthFile = XLALStringAppend( earthFile,
                                  "/share/lalpulsar/earth00-19-DE200.dat.gz" );
    sunFile = XLALStringAppend( sunFile,
                                "/share/lalpulsar/sun00-19-DE200.dat.gz" );
  }
  else if( strcmp(par.ephem, "DE405") ){
    earthFile = XLALStringAppend( earthFile,
                                  "/share/lalpulsar/earth00-19-DE405.dat.gz" );
    sunFile = XLALStringAppend( sunFile,
                                "/share/lalpulsar/sun00-19-DE405.dat.gz" );
  }
  
  /* static LALStatus status;
  edat = (EphemerisData *)LALCalloc(1, sizeof(EphemerisData));
  edat->ephiles.earthEphemeris = XLALStringDuplicate( earthFile );
  edat->ephiles.sunEphemeris = XLALStringDuplicate( sunFile );
  LALInitBarycenter( &status, edat ); */

  edat = XLALInitBarycenter( earthFile, sunFile );
  
  if ( verbose ) fprintf(stderr, "I've set up the ephemeris files\n");

  fpout = fopen("pulsarPhase.txt", "w");

  DM = params.DM;
  
  if( params.px != 0. ){
    baryinput.dInv = ( 3600. / LAL_PI_180 )*params.px /
     (LAL_C_SI*LAL_PC_SI/LAL_LYR_SI);
  }
  else baryinput.dInv = 0.0;  /* no parallax */
  
  if( params.units == NULL ) ttype = TYPE_TEMPO2;
  else if( !strcmp(params.units, "TDB") ) ttype = TYPE_TDB;
  else if( !strcmp(params.units, "TCB") ) ttype = TYPE_TCB; /* same as TYPE_TEMPO2 */
  else ttype = TYPE_TEMPO2; /*default */
  
  tcFile = XLALStringDuplicate(lalpath);

  /* read in the time correction file */
  if( ttype == TYPE_TEMPO2 || ttype == TYPE_TCB ){
    tcFile = XLALStringAppend( tcFile,
                               "/share/lalpulsar/te405_2000-2019.dat.gz" );
  }
  else if ( ttype == TYPE_TDB ){
    tcFile = XLALStringAppend( tcFile,
                               "/share/lalpulsar/tdb_2000-2019.dat.gz" ); 
  }
  
  tdat = XLALInitTimeCorrections( tcFile );

  for(j=0;j<i;j++){
    double t; /* DM for current pulsar - make more general */
    double deltaD_f2;
    double phase;
    double tt0;
    double phaseWave = 0., tWave = 0.;
    
    if (par.clock != NULL){
      while(MJD_tcorr[k] < TOA[j])
        k++;

      /* linearly interpolate between corrections */ 
      double grad = (tcorr[k] - tcorr[k-1])/(MJD_tcorr[k] - MJD_tcorr[k-1]);
      
      t = (TOA[j]-44244.)*86400. + (tcorr[k-1] + grad*(TOA[j] -
        MJD_tcorr[k-1]));
    }
    else t = (TOA[j]-44244.0)*86400.0;
    
    /* convert time from UTC to GPS reference */
    t += (double)XLALGPSLeapSeconds( (UINT4)t );
    
    baryinput.delta = params.dec + params.pmdec*(t-params.posepoch);
    baryinput.alpha = params.ra +
      params.pmra*(t-params.posepoch)/cos(baryinput.delta);
    
    /* set pulsar position */
    baryinput.delta = params.dec + params.pmdec*(t-params.posepoch);
    baryinput.alpha = params.ra +
      params.pmra*(t-params.posepoch)/cos(baryinput.delta);
    
    /* recalculate the time delay at the dedispersed time */
    XLALGPSSetREAL8( &baryinput.tgps, t );
    
    /* calculate solar system barycentre time delay */
    XLALBarycenterEarthNew( &earth, &baryinput.tgps, edat, tdat, ttype );
    XLALBarycenter( &emit, &baryinput, &earth );
    
    /* correct to infinite observation frequency for dispersion measure */
    rf[j] = rf[j] + rf[j]*(1.-emit.tDot);
   
    deltaD_f2 = DM / ( D * rf[j] * rf[j] );
    t -= deltaD_f2; /* dedisperse times */
    
    /* calculate binary barycentre time delay */
    input.tb = t + (double)emit.deltaT;
    
    if(params.model!=NULL){
      XLALBinaryPulsarDeltaT(&output, &input, &params);

      PPTime[j] = t + ((double)emit.deltaT + output.deltaT);
    }
    else
      PPTime[j] = t + (double)emit.deltaT;
    
    if( verbose ){
      fprintf(stderr, "%.12lf\n", 44244 + (PPTime[j] + 51.184)/86400.);
    }
    
    if(j==0){
      T = PPTime[0] - params.pepoch;
      f0 = params.f0 + params.f1*T + 0.5*params.f2*T*T + (1./6.)*params.f3*T*T*T;
      f1 = params.f1 + params.f2*T + 0.5*params.f3*T*T;
      f2 = params.f2 + params.f3*T;
      f3 = params.f3;
    }
    
    tt0 = PPTime[j] - PPTime[0];
    
    if( params.nwaves != 0 ){
      REAL8 dtWave = (XLALGPSGetREAL8(&emit.te) - params.waveepoch)/86400.;
      REAL8 om = params.wave_om;
        
      for( INT4 k = 0; k < params.nwaves; k++ ){
        tWave += params.waveSin[k]*sin(om*(REAL8)(k+1.)*dtWave) +
          params.waveCos[k]*cos(om*(REAL8)(k+1.)*dtWave);
      }
      phaseWave = params.f0*tWave;
    }
    
    phase = f0*tt0 + 0.5*f1*tt0*tt0 + f2*tt0*tt0*tt0/6.0 + f3*tt0*tt0*tt0*tt0/24.;
    
    phase = fmod(phase+phaseWave+0.5, 1.0) - 0.5;

    fprintf(fpout, "%.9lf\t%lf\n", tt0, phase);
  }

  fclose(fpout);

  return 0;
}

void get_input_args(Params *pars, int argc, char *argv[]){
  struct option long_options[] =
  {
    { "help",                     no_argument,        0, 'h' },
    { "par-file",                 required_argument,  0, 'p' },
    { "tim-file",                 required_argument,  0, 't' },
    { "ephemeris",                required_argument,  0, 'e' },
    { "clock",                    required_argument,  0, 'c' },
    { "simulated",                no_argument,        0, 's' },
    { "verbose",                  no_argument,     NULL, 'v' },
    { 0, 0, 0, 0 }
  };

  char args[] = "hp:t:e:c:sv";
  char *program = argv[0];
  
  pars->parfile = NULL;
  pars->timfile = NULL;
  pars->ephem = NULL;
  pars->clock = NULL;
  
  pars->simulated = 0;
  
  /* get input arguments */
  while(1){
    int option_index = 0;
    int c;

    c = getopt_long( argc, argv, args, long_options, &option_index );
    if ( c == -1 ) /* end of options */
      break;

    switch(c){
      case 0: /* if option set a flag, nothing else to do */
        if ( long_options[option_index].flag )
          break;
        else
          fprintf(stderr, "Error parsing option %s with argument %s\n",
            long_options[option_index].name, optarg );
      case 'h': /* help message */
        fprintf(stderr, USAGE, program);
        exit(0);
      case 'v': /* verbose output */
        verbose = 1;
        break;
      case 'p': /* par file */
        pars->parfile = XLALStringDuplicate( optarg );
        break;
      case 't': /* TEMPO2 timing file */
        pars->timfile = XLALStringDuplicate( optarg );
        break;
      case 'e': /* ephemeris (DE405/DE200) */
        pars->ephem = XLALStringDuplicate( optarg );
        break;
      case 'c': /* clock file */
        pars->clock = XLALStringDuplicate( optarg );
        break;
      case 's': /* simulated data */
        pars->simulated = 1;
        break;
      case '?':
        fprintf(stderr, "unknown error while parsing options\n" );
      default:
        fprintf(stderr, "unknown error while parsing options\n" );
    }
  }
  
  if (pars->parfile == NULL){
    fprintf(stderr, "Error... no .par file supplied!\n");
    exit(1);
  }
  
  if (pars->timfile == NULL){
    fprintf(stderr, "Error... no .tim file supplied!\n");
    exit(1);
  }
}
