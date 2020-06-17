#include <stdio.h>
#include <stdlib.h>
#include "../pinMarker.h"


int main(int argc, char** argv)
{

	unsigned int size = 2e6;
{

	//triad
	double* a =(double*)malloc(size*sizeof(double));
	double* b =(double*)malloc(size*sizeof(double));
	double* c =(double*)malloc(size*sizeof(double));
	double* d =(double*)malloc(size*sizeof(double));

	for (unsigned int i = 0; i < size; ++i)
	{
		a[i] = 1.01;
		b[i] = 1.02;
		c[i] = 1.03;
		d[i] = 1.04;
	}

	_magic_pin_start();
	for(unsigned int i = 0; i < size; ++i)
		a[i] = b[i] + c[i] * d[i];
	_magic_pin_stop();
	
	free(a);
	free(b);
	free(c);
	free(d);

}
{
	
	//scale
	double* a =(double*)malloc(size*sizeof(double));
	double* b =(double*)malloc(size*sizeof(double));
	double s = 0.98;

	for (unsigned int i = 0; i < size; ++i)
	{
		a[i] = 1.01;
		b[i] = 1.02;
	}

	for(unsigned int i = 0; i < size; ++i)
			a[i] = s * b[i];
	
	free(a);
	free(b);

}
{

	//sum
	double* a =(double*)malloc(size*sizeof(double));
	double* b =(double*)malloc(size*sizeof(double));
	double* c =(double*)malloc(size*sizeof(double));

	for (unsigned int i = 0; i < size; ++i)
	{
		a[i] = 1.01;
		b[i] = 1.02;
		c[i] = 1.03;
	}

	for(unsigned int i = 0; i < size; ++i)
		a[i] = b[i] + c[i];
	
	free(a);
	free(b);
	free(c);
}	
{
	
	//copy
	double* a =(double*)malloc(size*sizeof(double));
	double* b =(double*)malloc(size*sizeof(double));

	for (unsigned int i = 0; i < size; ++i)
	{
		a[i] = 1.01;
		b[i] = 1.02;
	}

	for(unsigned int i = 0; i < size; ++i)
		a[i] = b[i];
	
	free(a);
	free(b);

}

}

