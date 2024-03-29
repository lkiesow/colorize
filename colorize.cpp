/*******************************************************************************
 *
 *       Filename:  colorizeLaser.cpp
 *
 *    Description:  Takes the color information from one or more colored
 *    (kinect-) pointclouds and transfers these color informations to near
 *    points in an uncolored (laser-) cloud.
 *
 *        Version:  0.2
 *        Created:  07/02/2011 12:18:03 AM
 *       Compiler:  g++
 *
 *         Author:  Lars Kiesow (lkiesow), lkiesow@uos.de
 *        Company:  Universität Osnabrück
 *
 ******************************************************************************/

#include <pcl/point_cloud.h>
#include <pcl/kdtree/kdtree_flann.h>

#include <stdio.h>
#include <iostream>
#include <vector>
#include <ctime>

double maxdist = std::numeric_limits<double>::max();
char nc_rgb[64] = "  0   0   0";

using namespace pcl;


/*******************************************************************************
 *         Name:  readPts
 *  Description:  Load a pts file into memory.
 ******************************************************************************/
void readPts( char * filename, PointCloud<PointXYZRGB>::Ptr cloud ) {

	/* Open input file. */
	FILE * f = fopen( filename, "r" );
	if ( !f ) {
		fprintf( stderr, "error: Could not open »%s«.\n", filename );
		exit( EXIT_FAILURE );
	}

	/* Determine amount of values per line */
	char line[1024];
	fgets( line, 1023, f );
	int valcount = 0;
	char * pch = strtok( line, "\t " );
	while ( pch ) {
		if ( strcmp( pch, "" ) && strcmp( pch, "\n" ) ) {
			valcount++;
		}
		pch = strtok( NULL, "\t " );
	}

	/* Do we have color information in the pts file? */
	int read_color = valcount >= 6;
	/* Are there additional columns we dont want to have? */
	int dummy_count = valcount - ( read_color ? 6 : 3 );
	float dummy;

	/* Resize cloud. Keep old points. */
	int i = cloud->width;
	cloud->width += 100000;
	cloud->height = 1;
	cloud->points.resize( cloud->width * cloud->height );

	/* Start from the beginning */
	fseek( f, 0, SEEK_SET );

	/* Read values */
	while ( !feof( f ) ) {
		/* Read coordinates */
		fscanf( f, "%f %f %f", &cloud->points[i].x, &cloud->points[i].y,
				&cloud->points[i].z );

		/* Igbore remission, ... */
		for ( int j = 0; j < dummy_count; j++ ) {
			fscanf( f, "%f", &dummy );
			printf( "read dummy...\n" );
		}

		/* Read color information, if available */
		if ( read_color ) {
			uint32_t r, g, b;
			fscanf( f, "%u %u %u", &r, &g, &b );
			uint32_t rgb = r << 16 | g << 8 | b;
			cloud->points[i].rgb = *reinterpret_cast<float*>( &rgb );
		}
		i++;
		/* We have more points: enlarge cloud */
		if ( i >= cloud->points.size() ) {
			printf( "%u values read.\n", i );
			cloud->width = cloud->width + 100000;
			cloud->points.resize( cloud->width * cloud->height );
		}
	}
	i--;
	
	/* Resize cloud to amount of points. */
	cloud->width = i;
	cloud->points.resize( cloud->width * cloud->height );

	printf( "%u values read.\nPointcloud loaded.\n", i );

	if ( f ) {
		fclose( f );
	}



}


/*******************************************************************************
 *         Name:  printHelp
 *  Description:  Prints usage information.
 ******************************************************************************/
void printHelp( char * name ) {

	printf( "Usage: %s [options] laserdat kinectdat1 [kinectdat2 ...] outfile\n"
			"Options:\n"
			"   -h   Show this help and exit.\n"
			"   -d   Maximum distance for neighbourhood.\n"
			"   -j   Number of jobs to be scheduled parallel.\n"
			"        Positive integer or “auto” (default)\n"
			"   -c   Set color of points with no neighbours \n"
			"        as 24 bit hexadecimal integer.\n", name );

}


/*******************************************************************************
 *         Name:  printHelp
 *  Description:  Prints usage information.
 ******************************************************************************/
void parseArgs( int argc, char ** argv ) {

	/* Parse options */
	char c;
	while ( ( c = getopt( argc, argv, "hd:j:c:" ) ) != -1 ) {
		switch (c) {
			case 'h':
				printHelp( *argv );
				exit( EXIT_SUCCESS );
			case 'd':
				maxdist = atof( optarg );
				maxdist *= maxdist;
				break;
			case 'm':
				if ( !strcmp( optarg, "auto" ) ) {
					omp_set_num_threads( omp_get_num_procs() );
				} else {
					omp_set_num_threads( 
							atoi( optarg ) > 1 ? atoi( optarg ) 
						: omp_get_num_procs() );
				}
				break;
			case 'c':
				uint32_t rgb = 0;
				sscanf( optarg, "%x", &rgb );
				sprintf( nc_rgb, "% 3d % 3d % 3d", 
						(int) ((uint8_t *) &rgb)[2], 
						(int) ((uint8_t *) &rgb)[1], 
						(int) ((uint8_t *) &rgb)[0] );
				break;
		}
	}

	/* Check, if we got enough command line arguments */
	if ( argc - optind < 3 ) {
		printHelp( *argv );
		exit( EXIT_SUCCESS );
	}
	
}


/*******************************************************************************
 *         Name:  colorizeCloud
 *  Description:  
 ******************************************************************************/
void colorizeCloud( PointCloud<PointXYZRGB>::Ptr lasercloud, 
		PointCloud<PointXYZRGB>::Ptr kinectcloud, char * filename ) {

	/* Generate octree for kinect pointcloud */
	KdTreeFLANN<PointXYZRGB> kdtree; /* param: sorted */
	kdtree.setInputCloud( kinectcloud );

	/* Open output file. */
	FILE * out = fopen( filename, "w" );
	if ( !out ) {
		fprintf( stderr, "error: Could not open »%s«.\n", filename );
		exit( EXIT_FAILURE );
	}

	printf( "Adding color information...\n" );

	/* Run through laserscan cloud and find neighbours. */
	#pragma omp parallel for
	for ( int i = 0; i < lasercloud->points.size(); i++ ) {

		std::vector<int>   pointIdx(1);
		std::vector<float> pointSqrDist(1);

		/* nearest neighbor search */
		if ( kdtree.nearestKSearch( *lasercloud, i, 1, pointIdx, pointSqrDist ) ) {
			if ( pointSqrDist[0] > maxdist ) {
				fprintf( out, "% 11f % 11f % 11f % 14f 0 %s\n",
						lasercloud->points[i].x, lasercloud->points[i].y,
						lasercloud->points[i].z, pointSqrDist[0],
						nc_rgb );
			} else {
				uint8_t * rgb = (uint8_t *) &kinectcloud->points[ pointIdx[0] ].rgb;
				/* lasercloud->points[i].rgb = kinectcloud->points[ pointIdx[0] ].rgb; */
				fprintf( out, "% 11f % 11f % 11f % 14f 1 % 3d % 3d % 3d\n",
						lasercloud->points[i].x, lasercloud->points[i].y,
						lasercloud->points[i].z, pointSqrDist[0],
						rgb[2], rgb[1], rgb[0] );
			}

		}
	}

	if ( out ) {
		fclose( out );
	}

}


/*******************************************************************************
 *         Name:  main
 *  Description:  Main function.
 ******************************************************************************/
int main( int argc, char ** argv ) {

	omp_set_num_threads( omp_get_num_procs() );
	parseArgs( argc, argv );

	PointCloud<PointXYZRGB>::Ptr lasercloud(  new PointCloud<PointXYZRGB> );
	PointCloud<PointXYZRGB>::Ptr kinectcloud( new PointCloud<PointXYZRGB> );

	/* Read clouds from file. */
	printf( "Loading laserscan data...\n" );
	readPts( argv[optind], lasercloud );
	printf( "Loading kinect data...\n" );
	for ( int i = optind + 1; i < argc - 1; i++ ) {
		readPts( argv[i], kinectcloud );
	}

	colorizeCloud( lasercloud, kinectcloud, argv[ argc - 1 ] );

}
