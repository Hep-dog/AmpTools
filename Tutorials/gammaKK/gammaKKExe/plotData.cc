#include <iostream>
#include <string>
#include <vector>
#include "TString.h"
#include "TH1F.h"
#include "TH2F.h"
#include "TFile.h"
#include "TLorentzVector.h"

#include "IUAmpTools/Kinematics.h"
#include "gammaKKDataIO/gammaKKDataReader.h"

using namespace std;

int main(int argc, char** argv){


    // ************************
    // usage
    // ************************

  cout << endl << " *** Plotting Data *** " << endl << endl;

  if (argc <= 2){
    cout << "Usage:" << endl << endl;
    cout << "\tplotData <input file name> <output file name>" << endl << endl;
    return 0;
  }


    // ************************
    // parse the command line parameters
    // ************************

  string infilename(argv[1]);
  string outfilename(argv[2]);

  cout << "Input file name  = " << infilename << endl;
  cout << "Output file name = " << outfilename << endl << endl;


    // ************************
    // set up a gammaKKDataReader object
    // ************************

  cout << "Creating a Data Reader..." << endl;

  vector<string> args;
  args.push_back(infilename);
  gammaKKDataReader dataReader(args);

  cout << "... Finished creating a Data Reader" << endl << endl;


    // ************************
    // set up an output Root file
    // ************************

  TFile* plotfile = new TFile( outfilename.c_str(), "recreate");
  TH1::AddDirectory(kFALSE);


  TH1F* hm12 = new TH1F("hm12","hm12",60,0.0,3.0);
  hm12->SetStats(0);
  hm12->SetTitleOffset(1.9,"Y");
  hm12->SetTitleOffset(1.9,"X");
  hm12->SetTitle("IUAmpTools gammaKK Tutorial");
  hm12->SetXTitle("Mass(P_{1}P_{2}) (GeV/c^{2})");
  hm12->SetYTitle("Events / 5 MeV/c^{2}");

  TH1F* hm23 = new TH1F("hm23","hm23",60,0.0,3.0);
  hm23->SetStats(0);
  hm23->SetTitleOffset(1.9,"Y");
  hm23->SetTitleOffset(1.9,"X");
  hm23->SetTitle("IUAmpTools gammaKK Tutorial");
  hm23->SetXTitle("Mass(P_{2}P_{3}) (GeV/c^{2})");
  hm23->SetYTitle("Events / 5 MeV/c^{2}");

  TH1F* hm13 = new TH1F("hm13","hm13",60,0.0,3.0);
  hm13->SetStats(0);
  hm13->SetTitleOffset(1.9,"Y");
  hm13->SetTitleOffset(1.9,"X");
  hm13->SetTitle("IUAmpTools gammaKK Tutorial");
  hm13->SetXTitle("Mass(P_{1}P_{3}) (GeV/c^{2})");
  hm13->SetYTitle("Events / 5 MeV/c^{2}");

  TH2F* hs12s23 = new TH2F("hs12s23","hs12s23",50,0.0,8.0,50,0.0,8.0);
  hs12s23->SetStats(0);
  hs12s23->SetTitleOffset(1.9,"Y");
  hs12s23->SetTitleOffset(1.9,"X");
  hs12s23->SetTitle("IUAmpTools gammaKK Tutorial");
  hs12s23->SetXTitle("Mass^{2}(P_{2}P_{3}) (GeV/c^{2})");
  hs12s23->SetYTitle("Mass^{2}(P_{1}P_{2}) (GeV/c^{2})");

  TH1F *hPhotonCosTheta = new TH1F("hPhotonCosTheta",";cos#theta_{#gamma};",100,-1,1);
  hPhotonCosTheta->SetStats(0);
  hPhotonCosTheta->SetTitleOffset(1.9,"Y");
  hPhotonCosTheta->SetTitleOffset(1.9,"X");
  hPhotonCosTheta->SetTitle("IUAmpTools gammaKK Tutorial");
  hPhotonCosTheta->SetXTitle("cos#theta_{#gamma}");
  hPhotonCosTheta->SetYTitle("");

  TH1F *hMassKs = new TH1F("hMassKs",";M{K_{S}};",100,0.490,0.500);
  hMassKs->SetStats(0);
  hMassKs->SetTitleOffset(1.9,"Y");
  hMassKs->SetTitleOffset(1.9,"X");
  hMassKs->SetTitle("IUAmpTools gammaKK Tutorial");
  hMassKs->SetXTitle("M_{K_{S}}");
  hMassKs->SetYTitle("");

    // ************************
    // use the gammaKKDataReader object to read events 
    //  from a file and then fill histograms
    // ************************

  Kinematics* kin;

  while ( (kin = dataReader.getEvent()) ){

    vector<TLorentzVector> pList = kin->particleList();

    hm12->Fill((pList[0]+pList[1]).M());
    hm23->Fill((pList[1]+pList[2]).M());
    hm13->Fill((pList[0]+pList[2]).M());
    hs12s23->Fill((pList[1]+pList[2]).M2(),(pList[0]+pList[1]).M2());
    hPhotonCosTheta->Fill(pList[0].CosTheta());
    // Fill Ks mass for both Ks
    hMassKs->Fill(pList[1].M());
    hMassKs->Fill(pList[2].M());

    if (dataReader.eventCounter() % 100000 == 0)
      cout << "Event counter = " << dataReader.eventCounter() << endl;

    delete kin;

  }


    // ************************
    // write to the output Root file
    // ************************

  plotfile->cd();
  hm12->Write();
  hm23->Write();
  hm13->Write();
  hs12s23->Write();
  hPhotonCosTheta->Write();
  hMassKs->Write();
  plotfile->Close();

  delete plotfile;
  delete hm12;
  delete hm23;
  delete hm13;
  delete hs12s23;

}
