#include "dream.h"



int main(int argc, const char **argv)
{
	sem_init(&FrameReady, 0, 0);
	
	Params myparams;
	myparams.width=320;
	myparams.height=180;
	myparams.quality=85;
	myparams.wantRaw=0;
	myparams.timeout=50;
	
	
	SetUpCamera(myparams);
	
	pthread_create( &threadFrame, NULL, &WaitFrame, NULL);
	
	
	while(SetFrame())
	{
		// OPENCV CODE. THE IPLIMAGE POINTER TO USE IS "UsableFrame"
		

	}
	
	

}




