//----------------------------------------------------------------------------
//
// License:  See top level LICENSE.txt file.
//
// File: ossim-opencv.cpp
//
// Author:  Martina Di Rita
//
// Description: This plugIn is able to extract a geocoded Digital Surface Model
//				from a triplet.
//
//----------------------------------------------------------------------------

#include <ossim/base/ossimArgumentParser.h>
#include <ossim/base/ossimException.h>
#include <ossim/base/ossimRefPtr.h>
#include <ossim/base/ossimTrace.h>
#include <ossim/base/ossimGpt.h>
#include <ossim/base/ossimDpt.h>
#include <ossim/base/ossimKeywordlist.h>
#include <ossim/base/ossimKeywordNames.h>
#include <ossim/base/ossimStdOutProgress.h>

#include <ossim/elevation/ossimElevManager.h>

#include "ossim/imaging/ossimImageHandlerRegistry.h"
#include "ossim/imaging/ossimImageHandler.h"
#include "ossim/imaging/ossimImageGeometry.h"
#include "ossim/imaging/ossimImageFileWriter.h"
#include "ossim/imaging/ossimImageWriterFactoryRegistry.h"
#include <ossim/imaging/ossimMemoryImageSource.h>
#include <ossim/imaging/ossimTiffWriter.h>

#include <ossim/init/ossimInit.h>

#include <ossim/util/ossimChipperUtil.h>

#include "openCVtestclass.h"
#include "ossimDispMerging.h"
#include "ossimStereoPair.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdlib> /* for exit */
#include <iomanip>

#define C_TEXT( text ) ((char*)std::string( text ).c_str())

using namespace std;

static const std::string CUT_MAX_LAT_KW          = "cut_max_lat";
static const std::string CUT_MAX_LON_KW          = "cut_max_lon";
static const std::string CUT_MIN_LAT_KW          = "cut_min_lat";
static const std::string CUT_MIN_LON_KW          = "cut_min_lon";
static const std::string METERS_KW               = "meters";
static const std::string OP_KW                   = "operation";
static const std::string RESAMPLER_FILTER_KW     = "resampler_filter";
static const std::string PROJECTION_KW           = "projection";
static const std::string INPUT_NB_KW           = "input";

ossimImageHandler* raw_image_handler;


bool ortho (ossimKeywordlist kwl)
{
    // Make the generator
    ossimRefPtr<ossimChipperUtil> chipper = new ossimChipperUtil;
    chipper->initialize(kwl);

	try
	{      
		// ossimChipperUtil::execute can throw an exception
		chipper->execute();
		ossimNotify(ossimNotifyLevel_NOTICE)
		<< "elapsed time in seconds: "
		<< std::setiosflags(ios::fixed)
		<< std::setprecision(3)
		<< ossimTimer::instance()->time_s() << endl << endl;
	}
	catch (const ossimException& e)
	{
		ossimNotify(ossimNotifyLevel_WARN) << e.what() << endl;
		exit(1);
	}
	return true;
}

static ossimTrace traceDebug = ossimTrace("ossim-chipper:debug");

int main(int argc,  char* argv[])
{
	// Initialize ossim stuff, factories, plugin, etc.
	ossimTimer::instance()->setStartTick();
   	ossimArgumentParser ap(&argc, argv);
	ossimInit::instance()->initialize(ap);
	try
	{ 
		// PARSER *******************************
		cout << "Arg number " << ap.argc() << endl;

        // SISTEMARE, DEVE ESSERE <5, IL PIU GEN POSSIBILE (SOLO CON 2 IMMAGINI)
        if(ap.argc() < 5) //ap.argv[0] is the application name
        {
            ap.writeErrorMessages(ossimNotify(ossimNotifyLevel_NOTICE));
            std::string errMsg = "Few arguments...";
            cout << endl << "Usage: ossim-dsm-app <input_image_1> <input_image_2> ... <input_image_n> <output_results_directory> <output_dsm_name> [options] <n° steps for pyramidal>" << endl;
            cout << "Options:" << endl;
            cout << "--cut-bbox-ll <min_lat> <min_lon> <max_lat> <max_lon> \t Specify a bounding box with the minimum"   << endl;
            cout << "\t\t\t\t\t\t\tlatitude/longitude and max latitude/longitude" << endl;
            cout << "\t\t\t\t\t\t\tin decimal degrees." << endl;
            cout << "--meters <meters> \t\t\t\t\t Specify a size (in meters) for a resampling"   << endl<< endl;
            throw ossimException(errMsg);
        }

        // li definisco qui così ce l'ho a disposizione anche dopo
        ossimKeywordlist image_key;

        std::string tempString1,tempString2,tempString3,tempString4;
        ossimArgumentParser::ossimParameter stringParam1(tempString1);
        ossimArgumentParser::ossimParameter stringParam2(tempString2);
        ossimArgumentParser::ossimParameter stringParam3(tempString3);
        ossimArgumentParser::ossimParameter stringParam4(tempString4);

        double lat_min;
        double lon_min;
        double lat_max;
        double lon_max;
        double MinHeight;
        double MaxHeight;

        /**********************************************/
        /**********************************************/
        /************ BEGIN OF ARG PARSING ************/
        /**********************************************/
        /**********************************************/

        /***************************************************/
        /************ Default keyword for ortho ************/
        image_key.addPair(OP_KW, "ortho");

        /***********************************/
        /************ Resampler ************/
        image_key.addPair(RESAMPLER_FILTER_KW, "box");
        cout << endl << "Resampling filter is box" << endl << endl;

        /********************************/
        /************ Tiling ************/
        if( ap.read("--cut-bbox-ll", stringParam1, stringParam2, stringParam3, stringParam4) )
        {
            image_key.addPair( CUT_MIN_LAT_KW, tempString1 );
            image_key.addPair( CUT_MIN_LON_KW, tempString2 );
            image_key.addPair( CUT_MAX_LAT_KW, tempString3 );
            image_key.addPair( CUT_MAX_LON_KW, tempString4 );

            lat_min = atof(tempString1.c_str());
            lon_min = atof(tempString2.c_str());
            lat_max = atof(tempString3.c_str());
            lon_max = atof(tempString4.c_str());

            cout << "Tile extent:" << "\tLat_min = "<< lat_min << endl
                                    <<"\t\tLon_min = " << lon_min << endl
                                    <<"\t\tLat_max = " << lat_max << endl
                                    <<"\t\tLon_max = " << lon_max << endl << endl;

            /********** MIN and MAX HEIGHT COMPUTATION *********/
            std::vector<ossim_float64> HeightAboveMSL;
            for(double lat = lat_min; lat < lat_max; lat += 0.001)
            {
                for(double lon = lon_min; lon < lon_max; lon += 0.001)
                {
                    ossimGpt world_point(lat, lon, 0.00);
                    ossim_float64 hgtAboveMsl = ossimElevManager::instance()->getHeightAboveMSL(world_point);
                    HeightAboveMSL.push_back(hgtAboveMsl);
                }
            }

            MinHeight = *min_element(HeightAboveMSL.begin(), HeightAboveMSL.end());
            MaxHeight = *max_element(HeightAboveMSL.begin(), HeightAboveMSL.end());
            cout << "Min height for this tile is " << std::setprecision(6) << MinHeight << " m" << endl;
            cout << "Max height for this tile is " << std::setprecision(6) << MaxHeight << " m" << endl << endl;
        }

        /****************************************/
        /************ UTM projection ************/
        if(ap.read("--projection", stringParam1) )
        {
            //ossimKeywordlist projection; // creo questo ossimKeywordList di appoggio per poi riempire il vettore image_key
            image_key.addPair(PROJECTION_KW, tempString1);
            //image_key.push_back(projection);

            cout << "Output DSM is in UTM projection" << endl << endl;
        }

        /**********************************/
        /************ Sampling ************/
        if(ap.read("--meters", stringParam1) )
        {
            image_key.addPair(METERS_KW, tempString1 );
        }
        double finalRes = atof(tempString1.c_str());
        cout << "Orthoimages resolution = " << tempString1 <<" meters"<< endl << endl;

        /*********************************************/
        /************ Number of iteration ************/
        int nsteps;
        ossimArgumentParser::ossimParameter iteration(nsteps);
        if(ap.read("--nsteps", iteration))
        {
            //else nsteps = 1;
        }
        cout << "Total steps number for pyramidal:\t " << nsteps << endl << endl;

        ap.reportRemainingOptionsAsUnrecognized();
        if (ap.errors())
        {
            ap.writeErrorMessages(ossimNotify(ossimNotifyLevel_NOTICE));
            std::string errMsg = "Unknown option...";
            throw ossimException(errMsg);
        }

        /**********************************************/
        /**********************************************/
        /************ END OF ARG PARSING **************/
        /**********************************************/
        /**********************************************/

        //cout << image_key << endl << endl;


        // Reading a file from the terminal
        fstream f_input;
        f_input.open(ap[1], ios::in);

        if (f_input.fail())
        {
            cout << "Missing input file" << endl;
        }

        // Reading image path and pairs info from the text file
        ossimString imagePath;
        int id;
        int imagesNumb;
        vector<ossimString> imageList;
        vector<ossimStereoPair> StereoPairList;

        f_input >> imagesNumb;
        cout << "Number of images: " << imagesNumb << endl;

        //while (f_input >> id >> imagePath )// fino a che leggi un int seguito da due ossimString

        for (int i=0; i < imagesNumb ; i++)
        {
            f_input >> id >> imagePath;
            cout <<id << endl;
            cout << imagePath << endl;
            imageList.push_back(imagePath);
        }

        int pairsNumb;
        f_input >> pairsNumb;
        cout << endl << "Number of pairs: " << pairsNumb << endl << endl;

        // Riempio il vettore della coppia con info su id, path e fattore di conversione
        for (int i=0; i < pairsNumb ; i++)
        {
            int idMaster,idSlave;
            ossimStereoPair pair;
            f_input >> idMaster >>  idSlave;

            pair.setID(idMaster, idSlave);
            pair.setRawPath(imageList[idMaster],imageList[idSlave]);
            pair.computeConversionFactor(lon_max, lon_min, lat_max, lat_min, MinHeight, MaxHeight);
            StereoPairList.push_back(pair);

            cout << "Pair " << idMaster << idSlave <<endl;
            cout << "path master\t" << StereoPairList[i].getRawMasterPath() << endl;
            cout << "path slave\t" << StereoPairList[i].getRawSlavePath() << endl;
            cout << "conv factor pair: " << idMaster << idSlave << "\t" << StereoPairList[i].getConversionFactor() << endl<< endl << endl<< endl;
        }
       // f_input.close();

        //cout << StereoPairList[2].getRawMasterPath() << endl; // così entro nella path della master della coppia
        //cout << StereoPairList[2].getConversionFactor() << endl;

        // Leggo quante immagini ho in input
        cout << "Number of images to be processed: " << imageList.size() << endl << endl;

        // Per ottenere le path delle singole immagini elencate nel file
        cout << "dir_image_0 " << imageList[0] << endl;
        cout << "dir_image_1 " << imageList[1] << endl;
        cout << "dir_image_2 " << imageList[2] << endl;
        cout << "dir_image_3 " << imageList[3] << endl<< endl;

      /*  for (int i = 0; i <= pairsNumb  ; i++)
        {
            pair.computeConversionFactor(lon_max, lon_min, lat_max, lat_min, MinHeight, MaxHeight);
            double mean_conversionF = pair.getConversionFactor();
            cout << mean_conversionF << endl << endl;
        }*/

        /*******************************************************/
        /*******************************************************/
        /************ PYRAMIDAL ITERATION BEGINNING ************/
        /*******************************************************/
        /*******************************************************/
        double iterationLeft = (nsteps-1);

        for(int b = (nsteps-1) ; b >= 0  ; b--)
        {
            iterationLeft;

            // Elevation manager instance for coarse DSM reading
            ossimElevManager* elev = ossimElevManager::instance();
            cout << "elevation database \t" << elev->getNumberOfElevationDatabases() << endl;

            std::ostringstream strs;
            strs << iterationLeft;
            std::string Level = strs.str();

            double orthoRes = finalRes*pow (2, iterationLeft);
            std::ostringstream strsRes;
            strsRes << orthoRes;
            std::string ResParam = strsRes.str();

            cout << finalRes << " " << "m" << "\t final DSM resolution" << endl;
            cout << orthoRes << " " << "m" << "\t resolution of this level" << endl;
            cout << Level << "\t n° iterations left" << endl << endl<< endl;

            image_key.addPair(METERS_KW, ResParam);


            vector<ossimString> orthoList;
            for (int n=0; n < imagesNumb ; n++)
            {
                image_key.addPair("image1.file", imageList[n]);

                string Result;           // string which will contain the result
                ostringstream convert;   // stream used for the conversion
                convert << n;            // insert the textual representation of 'n' in the characters in the stream
                Result = convert.str();
                ossimString orthoPath = ossimFilename(ap[2]) + ossimString("ortho_images/") + ossimFilename(ap[3]) + ossimString("_level") + Level + ossimString("_image_") + Result + ossimString("_ortho.TIF");
                image_key.add( ossimKeywordNames::OUTPUT_FILE_KW, orthoPath);
                orthoList.push_back(orthoPath);

                cout << "ORTHO FOR LEVEL: "<< Level << endl << endl;
                cout << "path " << orthoPath << endl;
                //cout << n << endl << endl;
                cout << "Start orthorectification level " << Level << endl;
                ortho(image_key);
                //cout << image_key << endl << endl;
            }

            cout << endl << "UPDATED KEY: "<< endl<< endl;
            cout <<image_key << endl << endl;
            //cout << "ortholist" << orthoList[0] << endl << endl;

            // Riempio il vettore della coppia con info su path delle ortho

            for (int i=0; i < pairsNumb ; i++)
                {
                    //int idMaster,idSlave;
                    //f_input >> idMaster >>  idSlave;
                    //cout << "pippo " << orthoList[idMaster] << endl;
                    StereoPairList[i].setOrthoPath(orthoList[StereoPairList[i].get_id_master()], orthoList[StereoPairList[i].get_id_slave()]);

                    cout << "Pair " << StereoPairList[i].get_id_master() << StereoPairList[i].get_id_slave() <<endl;
                    cout << "path ortho master\t" << StereoPairList[i].getOrthoMasterPath() << endl;
                    cout << "path ortho slave\t" << StereoPairList[i].getOrthoSlavePath() << endl << endl;
                }


            iterationLeft--;

            // Raw images path

            /*cout << StereoPairList[0].getRawMasterPath() << endl;
            cout << StereoPairList[1].getRawMasterPath() << endl;
            cout << StereoPairList[2].getRawMasterPath() << endl;
            cout << StereoPairList[0].getRawSlavePath() << endl;
            cout << StereoPairList[1].getRawSlavePath() << endl;
            cout << StereoPairList[2].getRawSlavePath() << endl;*/

            //preparo vettore di stereo-coppie + fattore di conversione
            //StereoPairList[i].getConversionFactor();
            //StereoPairList[i].getOrthoMasterPath();
            //StereoPairList[i].getOrthoSlavePath();

            ossimDispMerging *mergedDisp = new ossimDispMerging() ;
            mergedDisp->execute(StereoPairList); // da qui voglio ottenere mappa di disparità fusa e metrica

            // Qui voglio sommare alla mappa di disparità fusa e metrica il dsm coarse
            // poi faccio il geocoding
            // poi esco da ciclo e rinizio a diversa risoluzione
            mergedDisp->computeDsm(StereoPairList, elev, b, ap); // genero e salvo il dsm finale

            cv::Mat FinalDisparity = mergedDisp->getMergedDisparity(); // questa è mappa di disparità fusa e metrica



/*
            // Qui voglio sommare alla mappa di disparità fusa e metrica il dsm coarse
            // poi faccio il geocoding
            // poi esco da ciclo e rinizio a diversa risoluzione

            // From Disparity to DSM
            ossimImageGeometry* master_geom = master_handler->getImageGeometry().get();
            master_handler->saveImageGeometry();

            double null_disp_threshold = 7.5;

            cout<< " " << endl << "DSM GENERATION \t wait few minutes..." << endl;
            cout << "null_disp_threshold"<< null_disp_threshold<< endl;

            for(int i=0; i< FinalDisparity.rows; i++)
            {
                for(int j=0; j< FinalDisparity.cols; j++)
                {
                    ossimDpt image_pt(j,i);
                    ossimGpt world_pt;

                    master_geom->localToWorld(image_pt, world_pt);

                    ossim_float64 hgtAboveMSL = elev->getHeightAboveMSL(world_pt);
                    //ossim_float64 hgtAboveMSL =  elev->getHeightAboveEllipsoid(world_pt); //Augusta site

                    if(FinalDisparity.at<double>(i,j) >= null_disp_threshold/abs(StereoPairList[i].getConversionFactor()))
                    {
                        FinalDisparity.at<double>(i,j) += hgtAboveMSL;


                        //hgtAboveMSL += FinalDisparity.at<double>(i,j);

                        //world_pt.height(hgtAboveMSL);

                        // image_points.push_back(world_pt);
                        // cout <<"punti"<<image_points[i]<<endl;
                    }
                    //To fill holes with DSM coarse
                    else
                    {
                        FinalDisparity.at<double>(i,j) = hgtAboveMSL;
                    }
                }
            }

            // Set the destination image size:
            ossimIpt image_size (FinalDisparity.cols , FinalDisparity.rows);
            ossimRefPtr<ossimImageData> finalDSM = ossimImageDataFactory::instance()->create(0, OSSIM_FLOAT32, 1, image_size.x, image_size.y);

            if(finalDSM.valid())
               finalDSM->initialize();
           // else
             //  return -1;

            for (int i=0; i< FinalDisparity.cols; i++) // for every column
            {
                for(int j=0; j< FinalDisparity.rows; j++) // for every row
                {
                    finalDSM->setValue(i,j,FinalDisparity.at<double>(j,i));
                }
            }


            ossimFilename pathDSM;
            if (b == 0)
               pathDSM = ossimFilename(ap[2]) + ossimString("DSM/") + ossimFilename(ap[3]) + ossimString(".TIF");
            else
                pathDSM = ossimFilename(ap[42]) + ossimString("temp_elevation/") + ossimFilename(ap[3])+ossimString(".TIF");

            // Create output image chain:
            ossimRefPtr<ossimMemoryImageSource> memSource = new ossimMemoryImageSource;
            memSource->setImage(finalDSM);
            memSource->setImageGeometry(master_geom);
            cout << "size" << master_geom->getImageSize() << endl;
            memSource->saveImageGeometry();

            ossimImageFileWriter* writer = ossimImageWriterFactoryRegistry::instance()->createWriter(pathDSM);
            writer->connectMyInputTo(0, memSource.get());
            writer->execute();

            writer->close();
            writer = 0;
            memSource = 0;*/


            delete mergedDisp;
            elev = 0;
        }
        iterationLeft --;

        f_input.close();


            /*  remove(ossimFilename(ossimFilename(ap[2]) + ossimString("temp_elevation/") + ossimFilename(ap[3])+ossimString(".TIF")));

                cout << "ciclo" << k << endl;

 /*            }
        }
        cout << endl << "D.A.T.E. Plug-in has successfully generated a Digital Surface Model from your triplet!\n" << endl;*/
    }
	catch (const ossimException& e)
	{
		ossimNotify(ossimNotifyLevel_WARN) << e.what() << endl;
        return 1;
	}
  
	return 0;
}
