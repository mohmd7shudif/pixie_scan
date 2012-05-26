/** \file QdcProcessor.hpp
 *
 * Handle some QDC action to determine positions in a strip detector
 */

#include <algorithm>
#include <fstream>
#include <iostream>
#include <numeric>
#include <sstream>
#include <vector>

#include "PositionProcessor.hpp"
#include "DetectorLibrary.hpp"
#include "RawEvent.h"

using namespace std;

namespace dammIds {
    namespace position {
	const int QDC_JUMP = 20;
	const int LOC_SUM  = 18;

	const int D_QDCNORMN_LOCX          = 2300;
	const int DD_QDCSUM__ENERGY_LOCX   = 4500;
	const int D_QDCTOTNORM_LOCX        = 2460;
	const int D_INFO_LOCX              = 2480;
	const int DD_QDCN__QDCN_LOCX       = 2500;
	const int DD_QDCTOT__QDCTOT_LOCX   = 2660;
	const int DD_POSITION__ENERGY_LOCX = 2680;
	const int DD_POSITION              = 2699;
    }
}

const string PositionProcessor::configFile("qdc.txt");

/**
 * Initialize the qdc to handle ssd events
 */
PositionProcessor::PositionProcessor() : EventProcessor()
{
    name="position";
    associatedTypes.insert("ssd");
}

/**
 * Reads in QDC parameters from an input file
 *   The file format allows comment lines at the beginning
 *   Followed by QDC lengths 
 *   Which QDC to use for position calculation
 *     followed by the amount to scale the [0,1] result by to physical units
 *   And min and max values of the normalized QDC for each location in form:
 *      <location> <min> <max>
 *   Note that QDC 0 is considered to be a baseline section of the trace for
 *     baseline removal for the other QDCs
 */
bool PositionProcessor::Init(DetectorDriver &driver)
{
    // Call the parent function to handle the standard stuff
    if (!EventProcessor::Init(driver)) {
        return false;
    }

    extern DetectorLibrary modChan;

    int numLocationsTop    = modChan.GetNextLocation("ssd", "top");
    int numLocationsBottom = modChan.GetNextLocation("ssd", "bottom");
    if (numLocationsTop != numLocationsBottom) {
        cerr << "Number of top positions (" << numLocationsTop 
            << ") does not match number of bottom positions ("
            << numLocationsBottom << ") in map!" << endl;
        cerr << "  Disabling QDC processor." << endl;

        return (initDone = false);
    }
    numLocations = numLocationsTop;
    if (numLocations > maxNumLocations) {
        cerr << "Number of positions (" << numLocations 
            << ") is larger then maximum number of supported positions ("
            << maxNumLocations << ") in PositionProcessor" << endl;
        cerr << "  Disabling QDC processor." << endl;
        return (initDone = false);
    }
    minNormQdc.resize(numLocations);
    maxNormQdc.resize(numLocations);

    ifstream in(configFile.c_str());
    if (!in) {
        cerr << "Failed to open the QDC parameter file, QDC processor disabled." << endl;
        return (initDone = false);	
    }

    // Ignore any lines at the beginning that don't have a digit
    // This allows for some comments at the beginning of the file
    int linesIgnored = 0;
    while ( !isdigit(in.peek()) ) {
        in.ignore(1000, '\n');
        linesIgnored++;
    }
    if (linesIgnored != 0) {
        cout << "Ignored " << linesIgnored << " comment lines in " 
            << configFile << endl;
    }

    for (int i=0; i < numQdcs; i++) 
        in >> qdcLen[i];
    partial_sum(qdcLen, qdcLen + numQdcs, qdcPos);
    totLen = qdcPos[numQdcs - 1]  - qdcLen[0];
    // totLen = accumulate(qdcLen + 1, qdcLen + 8, 0);
    
    in >> whichQdc >> posScale;
    
    int numLocationsRead = 0;
    while (true) {
        int location;
        in >> location;
        if (in.eof()) {
            // place this here so a trailing newline is okay in the config file
            break;
        }
        in >> minNormQdc[location] >> maxNormQdc[location];
        numLocationsRead++;
    }

    if (numLocationsRead != numLocations) {
        cerr << "Only read QDC position calibration information from "
            << numLocationsRead << " locations where a total of "
            << numLocations << " was expected!" << endl;
        cerr << "  Disabling position processor." << endl;
        return (initDone = false);
    }
    
    cout << "QDC processor initialized with " << numLocations 
         << " locations operating on " << numQdcs << " QDCs" << endl;
    cout << "QDC #" << whichQdc << " being used for position determination."
         << endl;

    return true;
}

/**
 *  Declare all the plots we plan on using (part of dammIds::qdc namespace)
 */
void PositionProcessor::DeclarePlots() const {
    using namespace dammIds::position;

    const int qdcBins = S8;
    const int normBins = SA;
    const int infoBins = S3;
    const int locationBins = S4;
    const int positionBins = S6;
    const int energyBins   = SA;

    for (int i = 0; i < maxNumLocations; ++i) {	
        stringstream str;
        for (int j = 0; j < numQdcs; ++j) {
            str << "QDC " << j << ", T/B LOC " << i;
            DeclareHistogram2D(DD_QDCN__QDCN_LOCX + QDC_JUMP * j + i , 
                               qdcBins, qdcBins, str.str().c_str() );
            str.str("");
            str << "QDC " << j << " NORM T/B LOC" << i;
            DeclareHistogram1D(D_QDCNORMN_LOCX + QDC_JUMP * j + i,
                               normBins, str.str().c_str() );
            str.str("");

        }
        str << "QDCTOT T/B LOC " << i;
        DeclareHistogram2D(DD_QDCTOT__QDCTOT_LOCX + i, qdcBins, qdcBins, str.str().c_str() );
        str.str("");

        str << "QDCTOT NORM T/B LOC " << i;
        DeclareHistogram1D(D_QDCTOTNORM_LOCX + i, normBins, str.str().c_str());
        str.str("");

        str << "INFO LOC " << i;
        DeclareHistogram1D(D_INFO_LOCX + i, infoBins, str.str().c_str());
        str.str("");
        
        str << "Energy vs. position, loc " << i;
        DeclareHistogram2D(DD_POSITION__ENERGY_LOCX + i, positionBins, energyBins, str.str().c_str());
        str.str("");

        /*
        str << "ALL QDCSUM vs. energy, loc " << i;
        DeclareHistogram2D(DD_QDCSUM__ENERGY_LOCX + i, SC, SC, str.str().c_str());
        str.str("");
        */
    }
    DeclareHistogram2D(DD_QDCTOT__QDCTOT_LOCX + LOC_SUM, qdcBins, qdcBins, "ALL QDCTOT T/B");
    DeclareHistogram1D(D_QDCTOTNORM_LOCX + LOC_SUM, normBins, "ALL QDCTOT NORM T/B");
    DeclareHistogram2D(DD_POSITION__ENERGY_LOCX + LOC_SUM, positionBins, energyBins, "All energy vs. position");
    // DeclareHistogram2D(DD_QDCSUM__ENERGY_LOCX + LOC_SUM, SC, SC, "ALL QDCSUM vs energy/10");

    DeclareHistogram1D(D_INFO_LOCX + LOC_SUM, infoBins, "ALL INFO");
    DeclareHistogram2D(DD_POSITION, locationBins, positionBins, "Qdc Position");
}

/**
 *  Process the QDC data involved in top/bottom side for a strip 
 *  Note QDC lengths are HARD-CODED at the moment for the plots and to determine the position
 */
bool PositionProcessor::Process(RawEvent &event) {
    if (!EventProcessor::Process(event))
        return false;

    static const vector<ChanEvent*> &sumEvents = 
	event.GetSummary("ssd:sum", true)->GetList();
    static const vector<ChanEvent*> &digisumEvents =
	event.GetSummary("ssd:digisum", true)->GetList();
    static const vector<ChanEvent*> &topEvents =
	event.GetSummary("ssd:top", true)->GetList();
    static const vector<ChanEvent*> &bottomEvents =
	event.GetSummary("ssd:bottom", true)->GetList();

    vector<ChanEvent *> allEvents;
    // just add in the digisum events for now
    allEvents.insert(allEvents.begin(), digisumEvents.begin(), digisumEvents.end());
    allEvents.insert(allEvents.begin(), sumEvents.begin(), sumEvents.end());

    for (vector<ChanEvent*>::const_iterator it = allEvents.begin();
	     it != allEvents.end(); ++it) {
         ChanEvent *sumchan   = *it;

        int location = sumchan->GetChanID().GetLocation();

        // Don't waste our time with noise events
        if ( (*it)->GetEnergy() < 10. || (*it)->GetEnergy() > 16374 ) {
            using namespace dammIds::position;

            // [7] -> Noise events
            plot(D_INFO_LOCX + location, 7);
            plot(D_INFO_LOCX + LOC_SUM , 7);
            continue;
        }

        const ChanEvent *top    = FindMatchingEdge(sumchan, topEvents.begin(), topEvents.end());
        const ChanEvent *bottom = FindMatchingEdge(sumchan, bottomEvents.begin(), bottomEvents.end());


        if (top == NULL || bottom == NULL) {
            using namespace dammIds::position;

            if (top == NULL) {
                // [6] -> Missing top
                plot(D_INFO_LOCX + location, 6);
                plot(D_INFO_LOCX + LOC_SUM, 6);
            }

            if (bottom == NULL) {
                // [5] -> Missing bottom
                plot(D_INFO_LOCX + location, 5);
                plot(D_INFO_LOCX + LOC_SUM, 5);
            }
            continue;
        }

        /* Make sure we get the same match going backwards to insure there is only one in the vector */
        if ( FindMatchingEdgeR(sumchan, topEvents.rbegin(), topEvents.rend()) != top) {
            using namespace dammIds::position;
            // [4] -> Multiple top
            plot(D_INFO_LOCX + location, 4);
            plot(D_INFO_LOCX + LOC_SUM , 4);
            continue;
        }
        if ( FindMatchingEdgeR(sumchan, bottomEvents.rbegin(), bottomEvents.rend()) != bottom) {
            using namespace dammIds::position;
            // [3] -> Multiple bottom
            plot(D_INFO_LOCX + location, 3);
            plot(D_INFO_LOCX + LOC_SUM , 3);
            continue;
        }

        using namespace dammIds::position;
        
        float topQdc[numQdcs];
        float bottomQdc[numQdcs];
        float topQdcTot = 0;
        float bottomQdcTot = 0;
        float position = NAN;
        
        topQdc[0] = top->GetQdcValue(0);
        bottomQdc[0] = bottom->GetQdcValue(0);
        if (bottomQdc[0] == U_DELIMITER || topQdc[0] == U_DELIMITER) {
            // This happens naturally for traces which have double triggers
            //   Onboard DSP does not write QDCs in this case
#ifdef VERBOSE
            cout << "SSD strip edges are missing QDC information for location " << location << endl;
#endif
            if (topQdc[0] == U_DELIMITER) {
                // [2] -> Missing top QDC
                plot(D_INFO_LOCX + location, 2);
                plot(D_INFO_LOCX + LOC_SUM, 2);
                // Recreate qdc from trace
                if ( !top->GetTrace().empty() ) {
                    topQdc[0] = accumulate(top->GetTrace().begin(), top->GetTrace().begin() + qdcLen[0], 0);
                } else {
                    topQdc[0] = 0;
                }
            }
            if (bottomQdc[0] == U_DELIMITER) {
                // [1] -> Missing bottom QDC
                plot(D_INFO_LOCX + location, 1);
                plot(D_INFO_LOCX + LOC_SUM, 1);		
                // Recreate qdc from trace
                if ( !bottom->GetTrace().empty() ) {
                    bottomQdc[0] = accumulate(bottom->GetTrace().begin(), bottom->GetTrace().begin() + qdcLen[0], 0);
                } else {
                    bottomQdc[0] = 0;
                }
            }
            if ( topQdc[0] == 0 || bottomQdc[0] == 0 ) {
                continue;
            }
        }
        // [0] -> good stuff
        plot(D_INFO_LOCX + location, 0);
        plot(D_INFO_LOCX + LOC_SUM , 0);

        for (int i = 0; i < numQdcs; ++i) {		
            if (top->GetQdcValue(i) == U_DELIMITER) {
                // Recreate qdc from trace
                topQdc[i] = accumulate(top->GetTrace().begin() + qdcPos[i-1],
                top->GetTrace().begin() + qdcPos[i], 0);
            } else {
                topQdc[i] = top->GetQdcValue(i);
            }

            topQdc[i] -= topQdc[0] * qdcLen[i] / qdcLen[0];
            topQdcTot += topQdc[i];
            topQdc[i] /= qdcLen[i];		
            
            if (bottom->GetQdcValue(i) == U_DELIMITER) {
                // Recreate qdc from trace
                bottomQdc[i] = accumulate(bottom->GetTrace().begin() + qdcPos[i-1],
                                          bottom->GetTrace().begin() + qdcPos[i], 0);
            } else {
                bottomQdc[i] = bottom->GetQdcValue(i);
            }

            bottomQdc[i] -= bottomQdc[0] * qdcLen[i] / qdcLen[0];
            bottomQdcTot += bottomQdc[i];
            bottomQdc[i] /= qdcLen[i];
            
            plot(DD_QDCN__QDCN_LOCX + QDC_JUMP * i + location, topQdc[i] + 100, bottomQdc[i] + 100);
            plot(DD_QDCN__QDCN_LOCX + QDC_JUMP * i + LOC_SUM, topQdc[i], bottomQdc[i]);
            
            float frac = topQdc[i] / (topQdc[i] + bottomQdc[i]) * 1000.; // per mil
            plot(D_QDCNORMN_LOCX + QDC_JUMP * i + location, frac);
            plot(D_QDCNORMN_LOCX + QDC_JUMP * i + LOC_SUM, frac);
            if (i == whichQdc) {
                position = posScale * (frac - minNormQdc[location]) / 
                    (maxNormQdc[location] - minNormQdc[location]);		
                sumchan->GetTrace().InsertValue("position", position);
                plot(DD_POSITION__ENERGY_LOCX + location, position, sumchan->GetCalEnergy());
                plot(DD_POSITION__ENERGY_LOCX + LOC_SUM, position, sumchan->GetCalEnergy());
            }
            if (i == 6 && !sumchan->IsSaturated()) {
                // compare the long qdc to the energy
                int qdcSum = topQdc[i] + bottomQdc[i];
                
                // MAGIC NUMBERS HERE, move to qdc.txt
                if (qdcSum < 1000 && sumchan->GetCalEnergy() > 15000) {
                    sumchan->GetTrace().InsertValue("badqdc", 1);
                } else {
                    plot(DD_POSITION, location, sumchan->GetTrace().GetValue("position"));
                }
                plot(DD_QDCSUM__ENERGY_LOCX + location, qdcSum, sumchan->GetCalEnergy() / 10);
                plot(DD_QDCSUM__ENERGY_LOCX + LOC_SUM , qdcSum, sumchan->GetCalEnergy() / 10);
            }
        } // end loop over qdcs
        topQdcTot    /= totLen;
        bottomQdcTot /= totLen;
        
        plot(DD_QDCTOT__QDCTOT_LOCX + location, topQdcTot, bottomQdcTot);
        plot(DD_QDCTOT__QDCTOT_LOCX + LOC_SUM , topQdcTot, bottomQdcTot);
    } // end iteration over sum events

   // test

    EndProcess();

    return true;
}

ChanEvent* PositionProcessor::FindMatchingEdge(ChanEvent *match,
					  vector<ChanEvent*>::const_iterator begin,
					  vector<ChanEvent*>::const_iterator end) const {
    const float timeCut = 5.; // maximum difference between edge and sum timestamps
	
    for (;begin < end; begin++) {
        if ( (*begin)->GetChanID().GetLocation() == match->GetChanID().GetLocation() &&
            abs( (*begin)->GetTime() - match->GetTime() ) < timeCut ) {
            return *begin;
        }
    }
    return NULL;
}

ChanEvent* PositionProcessor::FindMatchingEdgeR(ChanEvent *match,
					  vector<ChanEvent*>::const_reverse_iterator begin,
					  vector<ChanEvent*>::const_reverse_iterator end) const {
    const float timeCut = 5.; // maximum difference between edge and sum timestamps
	
    for (;begin < end; begin++) {
        if ( (*begin)->GetChanID().GetLocation() == match->GetChanID().GetLocation() &&
            abs( (*begin)->GetTime() - match->GetTime() ) < timeCut ) {
            return *begin;
        }
    }
    return NULL;
}
