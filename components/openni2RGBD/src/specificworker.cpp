/*
 *    Copyright (C) 2006-2010 by RoboLab - University of Extremadura
 *
 *    This file is part of RoboComp
 *
 *    RoboComp is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    RoboComp is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with RoboComp.  If not, see <http://www.gnu.org/licenses/>.
 */

 #include "specificworker.h"

/**
* \brief Default constructor
*/

SpecificWorker::SpecificWorker(MapPrx& mprx) : GenericWorker(mprx)
{
	openDevice();
	initializeStreams();

	registration=RoboCompRGBD::None;

	usersMutex = new QMutex();
	RGBMutex = new QMutex();
	depthMutex = new QMutex();
	pointsMutex = new QMutex();
	
	mColor = new uint16_t [IMAGE_WIDTH*IMAGE_HEIGHT*3];
	auxDepth = new uint8_t [IMAGE_WIDTH*IMAGE_HEIGHT*3];
	fps = 0;
	setPeriod(1);
}

void SpecificWorker::openDevice()
{
	openniRc = OpenNI::initialize();
	if (openniRc != openni::STATUS_OK)
	{
		OpenNI::shutdown();
		qFatal("openNi2Comp: OpenNI initialize failed: \n%s\n", OpenNI::getExtendedError());
	}

	openniRc = device.open(ANY_DEVICE);
	if (openniRc != openni::STATUS_OK)
	{
		OpenNI::shutdown();
		qFatal("openNi2Comp: Device open failed: \n%s\n", OpenNI::getExtendedError());
	}

	if(device.isFile())
	{
		qDebug("Es un archivo");
	}
	else
	{
		qDebug("Es un dispositivo fisico");
	}


        const openni::SensorInfo* depthInfo;
        const openni::SensorInfo* colorInfo;

        depthInfo = device.getSensorInfo(openni::SENSOR_DEPTH);
        const openni::Array<openni::VideoMode>& depthModes = depthInfo->getSupportedVideoModes();
	printf("Supported DEPTH modes:\n");
        for (int i = 0; i < depthModes.getSize(); ++i)
        {
                const openni::VideoMode* pSupportedMode = &depthModes[i];
                printf("%d x %d @ %d\n", pSupportedMode->getResolutionX(), pSupportedMode->getResolutionY(), pSupportedMode->getFps());
        }

        colorInfo = device.getSensorInfo(openni::SENSOR_COLOR);
        const openni::Array<openni::VideoMode>& colorModes = colorInfo->getSupportedVideoModes();
	printf("Supported COLOR modes:\n");
        for (int i = 0; i < colorModes.getSize(); ++i)
        {
                const openni::VideoMode* pSupportedMode = &colorModes[i];
                printf("%d x %d @ %d\n", pSupportedMode->getResolutionX(), pSupportedMode->getResolutionY(), pSupportedMode->getFps());
        }


}

void SpecificWorker::initializeStreams()
{

	if (!openStream(SENSOR_DEPTH, &depth)) qFatal("OPEN DEPTH STREAM FAILED!");

	if (!openStream(SENSOR_COLOR, &color)) qFatal("OPEN COLOR STREAM FAILED!");


	IMAGE_WIDTH=depth.getVideoMode().getResolutionX();
	IMAGE_HEIGHT=depth.getVideoMode().getResolutionY();
	printf("Using depth resolution:  %d x %d\n", IMAGE_WIDTH, IMAGE_HEIGHT);
	
	//RESIZE DOUBLE BUFFERS
	pointsBuff.resize(IMAGE_WIDTH*IMAGE_HEIGHT);
	depthBuff.resize(IMAGE_WIDTH*IMAGE_HEIGHT);

	depthBuffer = new vector<float>(IMAGE_WIDTH*IMAGE_HEIGHT);
	depthImage = new vector<float>(IMAGE_WIDTH*IMAGE_HEIGHT);
	colorImage = new vector<Ice::Byte>(IMAGE_WIDTH*IMAGE_HEIGHT*3); //x3 para RGB888Pixel
}

bool SpecificWorker::openStream(SensorType sensorType, VideoStream *stream)
{
	openniRc = stream->create(device, sensorType);
	if (openniRc != openni::STATUS_OK)
	{
		printf("openNi2Comp: Couldn't find stream:\n%s\n", OpenNI::getExtendedError());
		return false;
	}

	openniRc = stream->setMirroringEnabled(false);
        if (openniRc != openni::STATUS_OK)
        {
                printf("openNi2Comp: Couldn't disable mirrorirng:\n%s\n", OpenNI::getExtendedError());
                stream->destroy();
                return false;
        }


	openni::VideoMode mode;
        mode = stream->getVideoMode();
        mode.setResolution(640, 480);
        mode.setFps(30);
        openniRc = stream->setVideoMode(mode);
        if (openniRc != openni::STATUS_OK)
        {
                printf("openNi2Comp: Couldn't setVideoMode:\n%s\n", OpenNI::getExtendedError());
                stream->destroy();
                return false;
        }


	openniRc = stream->start();
	if (openniRc != openni::STATUS_OK)
	{
		printf("openNi2Comp: Couldn't start stream:\n%s\n", OpenNI::getExtendedError());
		stream->destroy();
		return false;
	}
	if (!stream->isValid())
		return false;

	return true;
}

/**
* \brief Default destructor
*/
SpecificWorker::~SpecificWorker()
{
	depth.stop(); depth.destroy();
	color.stop(); color.destroy();
	device.close();
	OpenNI::shutdown();

	delete depthBuffer;

	delete depthImage;
	delete colorImage;

	delete usersMutex;
	delete RGBMutex;
	delete depthMutex;
}


void SpecificWorker::compute( )
{
	static QTime reloj = QTime::currentTime();
	bool doCoordinates = readFrame();
	if (doCoordinates)
	{
		printf("compute<\n");
		computeCoordinates();
		printf(">compute\n");
		pointsBuff.swap();
	}
        if (reloj.elapsed() > 1000)
        {
		qDebug()<<"Grabbing at:"<<fps<<"fps";
/*
		double sum = 0;
		double div = 0;
		for (int i=0; i<pointsBuff.size; i++)
		{
			if (not isnan(pointsBuff[i].z))
			{
				sum += pointsBuff[i].z;
				div += 1;
			}
		}
		qDebug() << sum/div;
*/
		reloj.restart();
		fps=0;
       }
}

bool SpecificWorker::readFrame()
{
	openni::VideoStream* streams[] = {&depth, &color};

	int changedIndex = -1;
	bool doCoordinates = false;
	while (openni::OpenNI::waitForAnyStream(streams, 2, &changedIndex, 0) == openni::STATUS_OK)
	{
		switch (changedIndex)
		{
		case 0:
			readDepth();
			doCoordinates = true;
			fps++;
			break;
		case 1:
			readColor();
			break;
		default:
			printf("Error in wait\n");
		}
	}
	return doCoordinates;
}

void SpecificWorker::readDepth()
{
	openniRc = depth.readFrame(&depthFrame);
	if (openniRc != openni::STATUS_OK)
	{
		printf("Read depth failed!\n%s\n", OpenNI::getExtendedError());
		return;
	}
	if (depthFrame.getVideoMode().getPixelFormat() != PIXEL_FORMAT_DEPTH_1_MM && depthFrame.getVideoMode().getPixelFormat() != PIXEL_FORMAT_DEPTH_100_UM)
	{
		printf("Unexpected depth frame format\n");
		return;
	}

	pixDepth = (DepthPixel*)depthFrame.getData();
}

void SpecificWorker::readColor()
{
	openniRc = color.readFrame(&colorFrame);
	if (openniRc != openni::STATUS_OK)
	{
		printf("Read color failed!\n%s\n", OpenNI::getExtendedError());
		return;
	}

	if (colorFrame.getVideoMode().getPixelFormat() != PIXEL_FORMAT_RGB888)
	{
		printf("Unexpected color frame format\n");
		return;
	}

	uint8_t *pixColor = (uint8_t *)colorFrame.getData();

//	printf("%d %d\n", IMAGE_HEIGHT, IMAGE_WIDTH);
	RGBMutex->lock();
	memcpy(&colorImage->operator[](0),pixColor,IMAGE_HEIGHT*IMAGE_WIDTH*3);

	RGBMutex->unlock();
}

void SpecificWorker::computeCoordinates()
{
	//QMutexLocker l(pointsMutex); 57.00 43.00
	//static const float fovW = 57;
	//static const float fovH = 43;
	// focal: 522 sale abierto
	static const float flength_x = 545;// IMAGE_WIDTH / (2.f * tan( fovW / 2.0 ) );
	static const float flength_y = 545;// IMAGE_HEIGHT / (2.f * tan( fovH / 2.0 ) );
	//printf("%dx%d %f %f\n", IMAGE_WIDTH, IMAGE_HEIGHT, flength_x, flength_y);
	#pragma omp for
	for( int y=0 ; y<IMAGE_HEIGHT ; y++ ) 
	{
		for( int x=0 ; x<IMAGE_WIDTH ; x++ ) 
		{
			const int offset = y*IMAGE_WIDTH + x;
			
			//Copy to depth doublebuffer
			pixDepth[offset]*=1;
			depthBuff[offset] = pixDepth[offset];
			
			const float z = float(pixDepth[offset]);
		
			if( z < 0.1 ) 
			{
				//pixDepth[offset] = 8;
				pointsBuff[offset].x = NAN;
				pointsBuff[offset].y = NAN;
				pointsBuff[offset].z = NAN;
				pointsBuff[offset].w = NAN;
			} 
			else 
			{
				//(*depthImage)[offset] = z;
				pointsBuff[offset].x = z * (x - IMAGE_WIDTH/2) / flength_x;
				pointsBuff[offset].y = z * (IMAGE_HEIGHT/2- y) / flength_y;
				pointsBuff[offset].z = z;
				pointsBuff[offset].w = 1.0;
			}
		}
		
	}
}



bool SpecificWorker::setParams(RoboCompCommonBehavior::ParameterList params)
{
	timer.start(Period);
	return true;
}

void SpecificWorker::normalizeDepth()
{
	for (int i=0; i<(IMAGE_HEIGHT*IMAGE_WIDTH); i++)
	{
		normalDepth[i]=(255.-(255.*float(depthBuffer->operator[](i))/1000.));
		if (normalDepth[i] > 255) normalDepth[i] = 255;
		if (normalDepth[i] < 0) normalDepth[i] = 0;
	}
}

//////////////////////////////
/// SERVANT
//////////////////////////////

TRGBDParams SpecificWorker::getRGBDParams( )
{
	//¿CAMBIA ALGO DE ESTO DE UN FRAME A OTRO?
	TRGBDParams params;
	//params.color.focal=;
	params.color.width=IMAGE_WIDTH;
	params.color.height=IMAGE_HEIGHT;
	params.color.size=IMAGE_WIDTH*IMAGE_HEIGHT*3;
	//params.color.FPS=;
	//params.depth.focal=;
	params.depth.width=IMAGE_WIDTH;
	params.depth.height=IMAGE_HEIGHT;
	params.depth.size=IMAGE_WIDTH*IMAGE_HEIGHT;
	//params.depth.FPS=;¡
	params.timerPeriod=Period;
	//params.talkToBase=;
	//params.talkToJointMotor=;
	//params.driver=;
	//params.device=;
	return params;
}

void SpecificWorker::setRegistration (RoboCompRGBD::Registration value)
{
	registration=value;
}

Registration SpecificWorker::getRegistration ( ){
	return registration;
}

void SpecificWorker::getData(imgType& rgbMatrix, depthType& distanceMatrix, RoboCompJointMotor::MotorStateMap& hState, RoboCompDifferentialRobot::TBaseState& bState)
{
	RGBMutex->lock();
	rgbMatrix=*colorImage;
	RGBMutex->unlock();
	depthMutex->lock();
	distanceMatrix=*depthImage;
	depthMutex->unlock();

}

void SpecificWorker::getDepthInIR(depthType& distanceMatrix, RoboCompJointMotor::MotorStateMap &hState, RoboCompDifferentialRobot::TBaseState& bState)
{
	qDebug()<<"getDepthInIR not implemented yet";
}

void SpecificWorker::getImage(ColorSeq& color, DepthSeq& depth, PointSeq& points, RoboCompJointMotor::MotorStateMap &hState, RoboCompDifferentialRobot::TBaseState& bState)
{
	getDepth(depth,hState,bState);
	getRGB(color,hState,bState);
	getXYZ(points,hState,bState);
}

void SpecificWorker::getDepth(DepthSeq& depth, RoboCompJointMotor::MotorStateMap &hState, RoboCompDifferentialRobot::TBaseState& bState )
{
	depthBuff.copy(depth);
}

void SpecificWorker::getRGB(ColorSeq& color, RoboCompJointMotor::MotorStateMap &hState, RoboCompDifferentialRobot::TBaseState& bState)
{
        color.resize(IMAGE_WIDTH*IMAGE_HEIGHT);
	RGBMutex->lock();
	memcpy(&color[0], &colorImage->operator[](0), IMAGE_WIDTH*IMAGE_HEIGHT*3);
	RGBMutex->unlock();
}

void SpecificWorker::getXYZ(PointSeq& points, RoboCompJointMotor::MotorStateMap &hState, RoboCompDifferentialRobot::TBaseState& bState)
{
	pointsBuff.copy(points);
}



