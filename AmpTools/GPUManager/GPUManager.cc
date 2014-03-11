//******************************************************************************
// This file is part of AmpTools, a package for performing Amplitude Analysis
// 
// Copyright Trustees of Indiana University 2010, all rights reserved
// 
// This software written by Matthew Shepherd, Ryan Mitchell, and 
//                  Hrayr Matevosyan at Indiana University, Bloomington
// 
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions
// are met:
// 1. Redistributions of source code must retain the above copyright
//    notice and author attribution, this list of conditions and the
//    following disclaimer. 
// 2. Redistributions in binary form must reproduce the above copyright
//    notice and author attribution, this list of conditions and the
//    following disclaimer in the documentation and/or other materials
//    provided with the distribution.
// 3. Neither the name of the University nor the names of its contributors
//    may be used to endorse or promote products derived from this software
//    without specific prior written permission.
// 
// Creation of derivative forms of this software for commercial
// utilization may be subject to restriction; written permission may be
// obtained from the Trustees of Indiana University.
// 
// INDIANA UNIVERSITY AND THE AUTHORS MAKE NO REPRESENTATIONS OR WARRANTIES, 
// EXPRESS OR IMPLIED.  By way of example, but not limitation, INDIANA 
// UNIVERSITY MAKES NO REPRESENTATIONS OR WARRANTIES OF MERCANTABILITY OR 
// FITNESS FOR ANY PARTICULAR PURPOSE OR THAT THE USE OF THIS SOFTWARE OR 
// DOCUMENTATION WILL NOT INFRINGE ANY PATENTS, COPYRIGHTS, TRADEMARKS, 
// OR OTHER RIGHTS.  Neither Indiana University nor the authors shall be 
// held liable for any liability with respect to any claim by the user or 
// any other party arising from use of the program.
//******************************************************************************

#ifdef USE_MPI
#include <mpi.h>
#endif

#include <iostream>
#include <math.h>
#include <string.h>
#include <sys/time.h>
#include "cuda_runtime.h"

#include "IUAmpTools/AmplitudeManager.h"
#include "IUAmpTools/AmpVecs.h"

#include "GPUManager/GPUKernel.h"
#include "GPUManager/GPUManager.h"

#ifdef VTRACE
#include "vt_user.h"
#endif

bool GPUManager::m_cudaDisplay = false;

template <class T>
void reduce(int size, int threads, int blocks, T *d_idata, T *d_odata);

GPUManager::GPUManager() : 
m_ampCalcOnly( false )
{
  m_iNEvents=0;
  m_iNTrueEvents=0;
  
  m_iNAmps=0;
  m_iNAmpsH=0;
  m_iAmpArrSize=0;
  
  m_iEventArrSize=0;
  m_iVArrSize=0;
  
  //Host Arrays
  m_pfVRe=0;
  m_pfVIm=0;
  m_pfRes=0;
  
  //Device Arrays 
  m_pfDevData=0;
  m_pcDevCalcAmp=0;
  m_piDevPerm=0;
  m_pfDevAmps=0;
  m_pfDevWeights=0;
  m_pfDevVRe=0;
  m_pfDevVIm=0;
  m_pfDevResRe=0;
  m_pfDevResIm=0;
  m_pfDevREDUCE=0;
  
  //CUDA Thread and Grid sizes
  m_iDimGridX=0;
  m_iDimGridY=0;
  m_iDimThreadX=0;
  m_iDimThreadY=0;
  
  int thisDevice = 0;
  
  if( !m_cudaDisplay )
    cout<<"\n################### CUDA DEVICE ##################\n";    
  
#ifdef USE_MPI
  
  // Note that a better algorithm would be to utilize the jobs "local
  // rank" on the machine instead of global rank -- this needs development.
  // The obvious problem with the technique below is that, e.g., in a 
  // two-GPU per node cluster, all even rank jobs will use device zero.
  // If two even rank jobs land on the same node device zero will be
  // overloaded and device 1 will be unused.
  
  int rank, devs;
  MPI_Comm_rank( MPI_COMM_WORLD, &rank );
  cudaGetDeviceCount(&devs);
  thisDevice = rank % devs;
  
  if( !m_cudaDisplay ) {
    cout << "\nParallel GPU configuration requested." << endl;
    cout << "\nNumber of CUDA devices available on this node:  " << devs << endl;
    cout << "\nMPI process " << rank << " is using device " << thisDevice << endl;
  }
#endif
  
  ///////CUDA INITIALIZATION
  gpuErrChk( cudaSetDevice( thisDevice ) );
  cudaDeviceProp devProp;
  gpuErrChk( cudaGetDeviceProperties( &devProp, thisDevice ) );
  
  m_devProp_major = devProp.major;

  if( ! m_cudaDisplay ){
    
    cout<<"Current GPU Properites:\n";
    cout<<"\t Name: "<<devProp.name<<endl; 
    cout<<"\t Total global memory: "<<devProp.totalGlobalMem/((float)1024*1024)<<" MB"<<endl; 
    cout<<"\t Rev.: "<<devProp.major<<"."<<devProp.minor<<endl;
    cout<<"\t Precision (size of GDouble): " << sizeof(GDouble) << " bytes" << endl; 
    cout<<"##################################################\n\n";
    ///////END OF CUDA INITIALIZATION
    m_cudaDisplay = true;
  }
  
  if( m_devProp_major == 1  && devProp.minor < 3 ){
    
    // double precision operations need 1.3 hardware or higher
    assert( sizeof( GDouble ) <= 4 );
  }  
}

GPUManager::GPUManager( const AmpVecs& a )
{
  GPUManager();
  init( a );
}

GPUManager::~GPUManager()
{
  clearAll();
}


// Initialization routines:

void 
GPUManager::init( const AmpVecs& a )
{
  clearAll();
    
  // copy over some info from the AmpVecs object for array dimensions
  m_iNTrueEvents = a.m_iNTrueEvents;
  m_iNEvents     = a.m_iNEvents;
  m_iNParticles  = a.m_iNParticles;
  m_iNAmps       = a.m_iNTerms;
  
  // the rest of the data are derived:
  m_iEventArrSize     = sizeof(GDouble) * m_iNEvents;
  m_iTrueEventArrSize = sizeof(GDouble) * m_iNTrueEvents;
  
  // size of upper half of AiAj* matrix
  m_iNAmpsH = m_iNAmps * ( m_iNAmps + 1 ) / 2;
  
  // size needed to store amplitudes for each event
  //  m_iAmpArrSize     = sizeof(GDouble) * m_iNEvents * m_iNAmpsH;
  m_iAmpArrSize     = sizeof(GDouble) * m_iNEvents * m_iNAmps;
  
  // size of upper half of ViVj* matrix
  m_iVArrSize = sizeof(GDouble) * m_iNAmpsH;
  
  // host memory needed for intensity or integral calculation
  cudaMallocHost( (void**)&m_pfVRe   , m_iVArrSize     );
  cudaMallocHost( (void**)&m_pfVIm   , m_iVArrSize     );
  cudaMallocHost( (void**)&m_pfRes   , m_iEventArrSize );
    
  // device memory needed for intensity or integral calculation and sum
  gpuErrChk( cudaMalloc( (void**)&m_pfDevAmps    , 2 * m_iAmpArrSize ) ) ;
  gpuErrChk( cudaMalloc( (void**)&m_pfDevVRe     , m_iVArrSize       ) ) ;
  gpuErrChk( cudaMalloc( (void**)&m_pfDevVIm     , m_iVArrSize       ) ) ;
  gpuErrChk( cudaMalloc( (void**)&m_pfDevWeights , m_iEventArrSize   ) ) ;
  gpuErrChk( cudaMalloc( (void**)&m_pfDevResRe   , m_iEventArrSize   ) ) ;
  gpuErrChk( cudaMalloc( (void**)&m_pfDevResIm   , m_iEventArrSize   ) ) ;
  gpuErrChk( cudaMalloc( (void**)&m_pfDevREDUCE  , m_iEventArrSize   ) ) ;
  
  // allocate device memory needed for amplitude calculations
  gpuErrChk( cudaMalloc(  (void**)&m_pfDevData    , 4 * m_iNParticles * m_iEventArrSize ) ) ;
  gpuErrChk( cudaMalloc(  (void**)&m_pcDevCalcAmp , 2 * m_iEventArrSize                 ) ) ;
  gpuErrChk( cudaMalloc(  (void**)&m_piDevPerm    , m_iNParticles * sizeof( int )       ) ) ;
  
  cout << "GPU memory allocated for " << m_iNAmps << " amplitudes and "
       << m_iNEvents << " events (" << m_iNTrueEvents << " actual events)"
       << endl;
  
  //CUDA Dims
  calcCUDADims();
}


void
GPUManager::copyDataToGPU( const AmpVecs& a )
{  

#ifdef VTRACE
  VT_TRACER( "GPUManager::copyDataToGPU" );
#endif

  // make sure AmpVecs has been loaded with data
  assert( a.m_pdData );
  
  // copy the data into the device
  gpuErrChk( cudaMemcpy( m_pfDevData, a.m_pdData,
                         4 * m_iNParticles * m_iEventArrSize,
                        cudaMemcpyHostToDevice ) );

  // copy the weights to the GPU
  gpuErrChk( cudaMemcpy( m_pfDevWeights, a.m_pdWeights,
                         m_iEventArrSize, cudaMemcpyHostToDevice ) );
}


void
GPUManager::copyAmpsToGPU( const AmpVecs& a )
{

#ifdef VTRACE
  VT_TRACER( "GPUManager::copyAmpsToGPU" );
#endif

  gpuErrChk( cudaMemcpy( m_pfDevAmps, a.m_pdAmps,
                         2 * m_iAmpArrSize, cudaMemcpyHostToDevice ) );
}

void 
GPUManager::calcAmplitudeAll( const Amplitude* amp, GDouble* pcResAmp, 
                             const vector< vector< int > >* pvPermutations )
{
 
#ifdef VTRACE
  VT_TRACER( "GPUManager::calcAmplitudeAll" );
#endif
  
  dim3 dimBlock( m_iDimThreadX, m_iDimThreadY );
  dim3 dimGrid( m_iDimGridX, m_iDimGridY );
  
  // do the computation for all events for each permutation in the
  // vector of permunations
  vector< vector< int > >::const_iterator permItr = pvPermutations->begin();
  
  // if this is not true, AmplitudeManager hasn't been setup properly
  assert( permItr->size() == m_iNParticles );
  
  int permOffset = 0;
  for( ; permItr != pvPermutations->end(); ++permItr ){
    
    // copy the permutation to global memory
    gpuErrChk( cudaMemcpy( m_piDevPerm, &((*permItr)[0]),
                           m_iNParticles * sizeof( int ),
                           cudaMemcpyHostToDevice ) );
    
    // calculate amplitude factor for all events --
    // casting amp array to WCUComplex for 8 or 16 bit write 
    // operation of both real and complex parts at once
    
    amp->calcAmplitudeGPU( dimGrid, dimBlock, m_pfDevData, 
                          (WCUComplex*)m_pcDevCalcAmp, 
                          m_piDevPerm, m_iNParticles, m_iNEvents,
                          *permItr );
    
    // check to be sure kernel execution was OK
    cudaError_t cerrKernel=cudaGetLastError();
    if( cerrKernel!= cudaSuccess  ){
      
      cout << "\nKERNEL LAUNCH ERROR [" << amp->name() << "]: " 
           << cudaGetErrorString( cerrKernel ) << endl;
      assert( false );
    }
    
    // now copy the result out of the GPU into the correct place in the
    // pcResAmp array for this particular permutation
    gpuErrChk( cudaMemcpy( &(pcResAmp[permOffset]),
                           m_pcDevCalcAmp, 2 * m_iEventArrSize,
                           cudaMemcpyDeviceToHost ) );
        
    // increment the offset so that we place the computation for the
    // next permutation after the previous in pcResAmp
    permOffset += 2 * m_iNEvents;
  }    
}

double
GPUManager::calcSumLogIntensity( const vector< complex< double > >& prodCoef,
                                const vector< vector< bool > >& cohMtx )
{

#ifdef VTRACE
  VT_TRACER( "GPUManager::calcSumLogIntensity" );
#endif

  // be sure memory has been allocated for intensity computation
  assert( !m_ampCalcOnly );

  unsigned int i,j;
  
  // precompute the real and imaginary parts of ViVj* and copy to 
  // GPU global memory
  complex< double > cdFij;
  for( i = 0; i< m_iNAmps; i++) {
    for( j = 0; j <= i; j++ ) {
      
      cdFij = prodCoef[i] * conj( prodCoef[j] );
      
      // here is the transition from double -> GDouble
      m_pfVRe[i*(i+1)/2+j] = 
      ( cohMtx[i][j] ? static_cast< GDouble >( cdFij.real() ) : 0 );
      m_pfVIm[i*(i+1)/2+j] = 
      ( cohMtx[i][j] ? static_cast< GDouble >( cdFij.imag() ) : 0 );
    }
  }
  
  //Init global memory on GPU
  gpuErrChk( cudaMemcpyToSymbol( da_pfDevVRe_addr() , m_pfVRe     , m_iVArrSize ) );
  gpuErrChk( cudaMemcpyToSymbol( da_pfDevVIm_addr() , m_pfVIm     , m_iVArrSize ) );
  gpuErrChk( cudaMemcpyToSymbol( da_iNAmps_addr()  , &m_iNAmps  , sizeof(int) ) );
  gpuErrChk( cudaMemcpyToSymbol( da_iNEvents_addr() , &m_iNEvents , sizeof(int) ) );
    
  // compute the intensities
  dim3 dimBlock( m_iDimThreadX, m_iDimThreadY );
  dim3 dimGrid( m_iDimGridX, m_iDimGridY );
  GPU_ExecAmpKernel( dimGrid, dimBlock, m_pfDevAmps,
                     m_pfDevWeights, m_pfDevResRe );
  
  // Now the summation of the results -- do this on the CPU for small
  // numbers of events or cases where double precision GPU is not enabled

  double dGPUResult = 0;
  
  if( m_iNTrueEvents <= m_iNBlocks || sizeof( GDouble ) <= 4 )
  {
    gpuErrChk( cudaMemcpy( m_pfRes,m_pfDevResRe,
                          m_iTrueEventArrSize,cudaMemcpyDeviceToHost ) );
    for(i=0; i<m_iNTrueEvents; i++)
      dGPUResult += m_pfRes[i];
  }
  else
  {
    // zeroing out the padding as not to alter the results after the reduction
    gpuErrChk( cudaMemset( m_pfDevResRe+m_iNTrueEvents,0,
                           sizeof(GDouble)*(m_iNEvents-m_iNTrueEvents)) );
    // execute the kernel to sum partial sums from each block on CPU
    reduce<GDouble>(m_iNEvents, m_iNThreads, m_iNBlocks, m_pfDevResRe, m_pfDevREDUCE);
    
    // copy result from device to host
    gpuErrChk( cudaMemcpy( m_pfRes, m_pfDevREDUCE, m_iNBlocks*sizeof(GDouble),
                           cudaMemcpyDeviceToHost) );
    for(i=0; i<m_iNBlocks; i++)
      dGPUResult += m_pfRes[i];
  }
  
  return dGPUResult;
}

void
GPUManager::calcIntegral( GDouble* result, int iAmp, int jAmp, int iNGenEvents ){
  
#ifdef VTRACE
  VT_TRACER( "GPUManager::calcIntegral" );
#endif

  gpuErrChk( cudaMemcpyToSymbol( da_iNEvents_addr() , &m_iNEvents , sizeof(int) ) );

  dim3 dimBlock( m_iDimThreadX, m_iDimThreadY );
  dim3 dimGrid( m_iDimGridX, m_iDimGridY );
  
  GPU_ExecIntElementKernel( dimGrid, dimBlock, iAmp, jAmp, m_pfDevAmps,
                             m_pfDevWeights, m_pfDevResRe, m_pfDevResIm );

  // add up the real parts
  result[0] = 0;

  if( m_iNTrueEvents <= m_iNBlocks || sizeof( GDouble ) <= 4 )
  {
    gpuErrChk( cudaMemcpy( m_pfRes, m_pfDevResRe,
                           m_iTrueEventArrSize, cudaMemcpyDeviceToHost ) );
    
    for(int i=0; i<m_iNTrueEvents; i++){

      result[0] += m_pfRes[i];
    }

    result[0] /= static_cast< GDouble >( iNGenEvents );
  }
  else
  {
    gpuErrChk( cudaMemset( m_pfDevResRe + m_iNTrueEvents, 0,
                          sizeof(GDouble)*( m_iNEvents - m_iNTrueEvents ) ) );
    
    // execute the kernel to sum partial sums from each block on GPU
    reduce<GDouble>(m_iNEvents, m_iNThreads, m_iNBlocks, m_pfDevResRe, m_pfDevREDUCE);
    
    // copy result from device to host
    gpuErrChk( cudaMemcpy( m_pfRes, m_pfDevREDUCE, m_iNBlocks*sizeof(GDouble),
                           cudaMemcpyDeviceToHost) );

    for(int i = 0; i < m_iNBlocks; i++){

      result[0] += m_pfRes[i];
    }

    result[0] /= static_cast< GDouble >( iNGenEvents );
  }

  // repeat for imaginary parts
  result[1] = 0;

  if( m_iNTrueEvents <= m_iNBlocks || sizeof( GDouble ) <= 4 ) {

    gpuErrChk( cudaMemcpy( m_pfRes, m_pfDevResIm,
			   m_iTrueEventArrSize, cudaMemcpyDeviceToHost ) );

    for(int i=0; i<m_iNTrueEvents; i++){
    
      result[1] += m_pfRes[i];
    }
    
    result[1] /= static_cast< GDouble >( iNGenEvents );
  }
  else {

    gpuErrChk( cudaMemset( m_pfDevResIm + m_iNTrueEvents, 0,
                          sizeof(GDouble)*( m_iNEvents - m_iNTrueEvents ) ) );
    
    // execute the kernel to sum partial sums from each block on GPU
    reduce<GDouble>(m_iNEvents, m_iNThreads, m_iNBlocks, m_pfDevResIm, m_pfDevREDUCE);
    
    // copy result from device to host
    gpuErrChk( cudaMemcpy( m_pfRes, m_pfDevREDUCE, m_iNBlocks*sizeof(GDouble),
                          cudaMemcpyDeviceToHost) );
    for(int i = 0; i < m_iNBlocks; i++){

      result[1] += m_pfRes[i];
    }

    result[1] /= static_cast< GDouble >( iNGenEvents );
  }
}


// Methods to clear memory:

void GPUManager::clearAll()
{
  clearAmpCalc();
  
  if( !m_ampCalcOnly ) clearLikeCalc();
}

void GPUManager::clearAmpCalc()
{
  m_iNParticles=0;
  m_iNEvents=0;
  m_iNTrueEvents=0;
  m_iEventArrSize=0;
  m_iTrueEventArrSize=0;
  
  //Device Memory 
  if(m_pfDevData)
    cudaFree(m_pfDevData);
  m_pfDevData=0;
  
  if(m_pcDevCalcAmp)
    cudaFree(m_pcDevCalcAmp);
  m_pcDevCalcAmp=0;
  
  if(m_piDevPerm)
    cudaFree(m_piDevPerm);
  m_piDevPerm=0;
}

void GPUManager::clearLikeCalc()
{
  m_iNEvents=0;
  m_iNTrueEvents=0;
  m_iNAmps=0;
  m_iNAmpsH=0;
  
  m_iEventArrSize=0;
  m_iTrueEventArrSize=0;
  m_iAmpArrSize=0;
  m_iVArrSize=0;
  
  //Host Memory
  
  //Allocated pointers
  
  if(m_pfVRe)
    cudaFreeHost(m_pfVRe);
  m_pfVRe=0;
  
  if(m_pfVIm)
    cudaFreeHost(m_pfVIm);
  m_pfVIm=0;
  
  if(m_pfRes)
    cudaFreeHost(m_pfRes);
  m_pfRes=0;
  
  //Device Memory 
  
  if(m_pfDevAmps)
    cudaFree(m_pfDevAmps);
  m_pfDevAmps=0;
  
  if(m_pfDevVRe)
    cudaFree(m_pfDevVRe);
  m_pfDevVRe=0;
  
  if(m_pfDevVIm)
    cudaFree(m_pfDevVIm);
  m_pfDevVIm=0;
  
  if(m_pfDevWeights)
    cudaFree(m_pfDevWeights);
  m_pfDevWeights=0;
  
  if(m_pfDevResRe)
    cudaFree(m_pfDevResRe);
  m_pfDevResRe=0;

  if(m_pfDevResRe)
    cudaFree(m_pfDevResIm);
  m_pfDevResRe=0;

  if(m_pfDevREDUCE)
    cudaFree(m_pfDevREDUCE);
  m_pfDevREDUCE=0;
  
  //CUDA Thread and Grid sizes
  m_iDimGridX=0;
  m_iDimGridY=0;
  m_iDimThreadX=0;
  m_iDimThreadY=0;
}

// Internal utilities:
void GPUManager::calcCUDADims()
{
  if(m_iNEvents<1)
    return;
  
  m_iDimThreadX=GPU_BLOCK_SIZE_X;
  m_iDimThreadY=GPU_BLOCK_SIZE_Y; 
  
  unsigned int iBlockSizeSq=GPU_BLOCK_SIZE_SQ;
  unsigned int iNBlocks=m_iNEvents/iBlockSizeSq;
  if(iNBlocks<=1)
  {
    m_iDimGridX=1;
    m_iDimGridY=1;
  }
  else
  {
    unsigned int iDivLo=1,iDivHi=iNBlocks;
    for(iDivLo=static_cast<int>(sqrt(iNBlocks));iDivLo>=1;iDivLo--)
    {
      iDivHi=iNBlocks/iDivLo;
      if(iDivLo*iDivHi==iNBlocks)
        break;
    }
    m_iDimGridX=iDivLo;
    m_iDimGridY=iDivHi;
  }
  
  // cout<<"\tThread dimensions:  ("<<m_iDimThreadX<<","<<m_iDimThreadY<<")\n";
  // cout<<"\tGrid dimensions:  ("<<m_iDimGridX<<","<<m_iDimGridY<<")\n";
  
  //Reduction Parameters
  unsigned int maxThreads = ( m_devProp_major >= 2 ? 1024 : 512 );  // number of threads per block
  unsigned int maxBlocks = 1024;  
  
  if (m_iNEvents == 1) 
    m_iNThreads = 1;
  else
    m_iNThreads = (m_iNEvents < maxThreads*2) ? m_iNEvents / 2 : maxThreads;
  
  m_iNBlocks = m_iNEvents / (m_iNThreads * 2); 
  m_iNBlocks = min(maxBlocks, m_iNBlocks);
  
  // cout<<"Reduction:\n";
  // cout<<"\tNumber of threads:  "<<m_iNThreads<<"\n";
  // cout<<"\tNumber of blocks:   "<<m_iNBlocks<<"\n\n\n"<<flush; 
}

