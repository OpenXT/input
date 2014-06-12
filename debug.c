/*
 * Copyright (c) 2012 Citrix Systems, Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "project.h"

#ifdef debug

#define KEY_MOUSE_START 0x110
char keyastr[][7]={"LEFT","RIGHT","MIDDLE","SIDE","EXTRA","FORWARD","BACK","TASK"};


#define KEY_DIGI_START 0x140
char keybstr[][15]={"TOOL_PEN","TOOL_RUBBER","TOOL_BRUSH","TOOL_PENCIL","TOOL_AIRBRUSH","TOOL_FINGER","TOOL_MOUSE","TOOL_LENS","TOOL_QUINTTAP","149","TOUCH","STYLUS","STYLUS2","TOOL_DOUBLETAP","TOOL_TRIPLETAP","TOOL_QUADTAP"};	

char typestr[][4] = {"SNC", "KEY", "REL", "ABS", "MSC", "SW "};

#define RELELEMENTS 10
char relstr[][7]={"X","Y","Z","RX","RY","RZ","HWHEEL","DIAL","WHEEL","MISC"};

#define ABSELEMENTS 0x3b
char absstr[][15]={
"X","Y","Z","RX","RY","RZ","THROTTLE","RUDDER","WHEEL","GAS","BRAKE"
,"0b", "0c", "0d", "0e", "0f",
"HAT0X","HAT0Y","HAT1X","HAT1Y","HAT2X","HAT2Y","HAT3X","HAT3Y","PRESSURE","DISTANCE","TILT_X","TILT_Y","TOOL_WIDTH","1d","1e","1f","Volume","21","22","23","24","25","26","27","MISC"
,"29", "2a", "2b", "2c", "2d", "2e",
"MT_SLOT","MT_TOUCH_MAJOR","MT_TOUCH_MINOR","MT_WIDTH_MAJOR","MT_WIDTH_MINOR","MT_ORIENTATION","MT_POSITION_X","MT_POSITION_Y","MT_TOOL_TYPE","MT_BLOB_ID","MT_TRACKING_ID","MT_PRESSURE","MT_DISTANCE"}; 



void print_abs_bit_meaning(unsigned long* inbit)
{
  int words= ABS_WORDS;
  int i,word;
  uint32_t bit = 1;
  int index=0;

 info("ABS bits are 0x%016llX and decode as:\n", *((uint64_t*)inbit ));

for (word=0; word<words; word++)
{
bit =1;

   for (i=0; (i<32) && (index<=ABSELEMENTS) ; i++)
   {
      if (inbit[word] & bit)
		info(absstr[index]);
      bit <<= 1;
      index++;
    }
}


}



void print_rel_bit_meaning(unsigned long* inbit)
{
  int i;
  uint32_t bit = 1;

   info("rel bits are 0x%08X and decode as:\n", *inbit );

   for (i=0; (i<RELELEMENTS) ; i++)
   {
      if (*inbit & bit)
		info(relstr[i]);
      bit <<= 1;
    }
}


void print_btn_bit_meaning(unsigned long* inbit)
{
  int words= BTN_WORDS;
  int i,word;
  uint32_t bit = 1;
  int index=0;

info ("BTN Bits are 0x%08x %08x %08x, and decode as:\n",inbit[2], inbit[1], inbit[0]);

for (word=0; word<words; word++)
{
bit =1;

   for (i=0; i<32 ; i++)
   {
      if (inbit[word] & bit)
		{
		if ((index+0x100 >= (KEY_MOUSE_START)) && (index < 0x18))
			info(keyastr[index+0x100 -KEY_MOUSE_START]);
		else if ((index+0x100 >= (KEY_DIGI_START)) && (index < 0x50))
			info(keybstr[index+0x100 -KEY_DIGI_START]);
		else
			info("button 0x%x", index+0x100);
		}
      bit <<= 1;
      index++;
    }
}

}


#ifdef debug_packets

void debug_packet(int slot, struct input_event *e)
{
   char outstr[80];
   char tmp[40];

   if ((e->type==0) && (e->code==0))
 	strcpy(outstr, "-sync----------\n");
   else if ((e->type==1) && (e->code>(KEY_MOUSE_START-1)) && (e->code<0x118))
	sprintf(outstr, "Button %s v %x\n", keyastr[e->code-KEY_MOUSE_START], e->value);
   else if ((e->type==1) && (e->code>(KEY_DIGI_START-1)) && (e->code<0x150))
	sprintf(outstr, "Button %s v %x\n", keybstr[e->code-KEY_DIGI_START], e->value);
   else if ((e->type==2) &&  (e->code<0xa))
	sprintf(outstr, "Rel %s v %x\n", relstr[e->code], e->value);
   else if ((e->type==3) && (e->code<0x3c))
	sprintf(outstr, "Abs %s v %x\n", absstr[e->code], e->value);
   else
	sprintf(outstr, "%s c %x v %x\n", (e->type)<6?typestr[e->type]:"???", e->code, e->value);
	
	info("%d: %s",slot, outstr);
}

#endif
#endif

