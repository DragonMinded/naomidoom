//
//  i_mus_convert.cpp
//  DoomTV
//
//  Created by Kristoffer Andersen on 21/02/2019.
//  Copyright © 2019 Kristoffer Andersen. All rights reserved.
//

#include "i_mus_convert.hpp"
#include "mus2midi.h"

#ifdef NAOMI
extern "C" {
#endif

int convertToMidi(void *musData, void **midiOutput, int *sizeOutput) {
    MUSHeader *header = (MUSHeader*) musData;
    
    TArray<BYTE> midiData;
    bool success = ProduceMIDI((BYTE*)musData, header->SongStart + header->SongLen, midiData);
    
    if (!success) return 0;
    
    int bytes = midiData.Size();
    *midiOutput = malloc(bytes);
    BYTE *byteData = (BYTE*) *midiOutput;
    for(int i=0;i<bytes;i++) {
       byteData[i] = midiData[i];
    }
    
    *sizeOutput = bytes;
    return 1;
}

#ifdef NAOMI
}
#endif
