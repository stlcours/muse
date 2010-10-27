//=========================================================
//  MusE
//  Linux Music Editor
//  $Id: plugin.cpp,v 1.21.2.23 2009/12/15 22:07:12 spamatica Exp $
//
//  (C) Copyright 2000 Werner Schweer (ws@seh.de)
//=========================================================

#include <qdir.h>
//Added by qt3to4:
#include <QBoxLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <Qt3Support>
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <cmath>
#include <math.h>

#include <qwidget.h>
#include <QLayout>
#include <qlabel.h>
#include <qsignalmapper.h>
#include <qpushbutton.h>
#include <q3scrollview.h>
#include <q3listview.h>
//#include <q3toolbar.h>
#include <QToolBar>
#include <qtoolbutton.h>
#include <q3whatsthis.h>
#include <qcheckbox.h>
#include <qtooltip.h>
//#include <qwidgetfactory.h>
#include <qfile.h>
#include <qobject.h>
#include <qcombobox.h>
#include <QButtonGroup>
#include <QGroupBox>
#include <qradiobutton.h>
#include <qmessagebox.h>
#include <qtimer.h>

#include "globals.h"
#include "gconfig.h"
#include "filedialog.h"
#include "slider.h"
#include "midictrl.h"
#include "plugin.h"
#include "xml.h"
#include "icons.h"
#include "song.h"
#include "doublelabel.h"
#include "fastlog.h"
#include "checkbox.h"

#include "audio.h"
#include "al/dsp.h"

#include "config.h"

// Turn on debugging messages.
//#define PLUGIN_DEBUGIN 

PluginList plugins;

/*
static const char* preset_file_pattern[] = {
      QT_TR_NOOP("Presets (*.pre *.pre.gz *.pre.bz2)"),
      QT_TR_NOOP("All Files (*)"),
      0
      };

static const char* preset_file_save_pattern[] = {
      QT_TR_NOOP("Presets (*.pre)"),
      QT_TR_NOOP("gzip compressed presets (*.pre.gz)"),
      QT_TR_NOOP("bzip2 compressed presets (*.pre.bz2)"),
      QT_TR_NOOP("All Files (*)"),
      0
      };
*/

int PluginDialog::selectedPlugType = 0;
QStringList PluginDialog::sortItems = QStringList();

//---------------------------------------------------------
//   ladspa2MidiControlValues
//---------------------------------------------------------

bool ladspa2MidiControlValues(const LADSPA_Descriptor* plugin, int port, int ctlnum, int* min, int* max, int* def)
{
  LADSPA_PortRangeHint range = plugin->PortRangeHints[port];
  LADSPA_PortRangeHintDescriptor desc = range.HintDescriptor;
  
  float fmin, fmax, fdef;
  int   imin, imax;
  float frng;
  //int idef;
  
  //ladspaControlRange(plugin, port, &fmin, &fmax);
  
  bool hasdef = ladspaDefaultValue(plugin, port, &fdef); 
  //bool isint = desc & LADSPA_HINT_INTEGER;
  MidiController::ControllerType t = midiControllerType(ctlnum);
  
  #ifdef PLUGIN_DEBUGIN 
  printf("ladspa2MidiControlValues: ctlnum:%d ladspa port:%d has default?:%d default:%f\n", ctlnum, port, hasdef, fdef);
  #endif
  
  if(desc & LADSPA_HINT_TOGGLED) 
  {
    #ifdef PLUGIN_DEBUGIN 
    printf("ladspa2MidiControlValues: has LADSPA_HINT_TOGGLED\n");
    #endif
    
    *min = 0;
    *max = 1;
    *def = (int)lrint(fdef);
    return hasdef;
  }
  
  float m = 1.0;
  if(desc & LADSPA_HINT_SAMPLE_RATE)
  {
    #ifdef PLUGIN_DEBUGIN 
    printf("ladspa2MidiControlValues: has LADSPA_HINT_SAMPLE_RATE\n");
    #endif
    
    m = float(sampleRate);
  }  
  
  if(desc & LADSPA_HINT_BOUNDED_BELOW)
  {
    #ifdef PLUGIN_DEBUGIN 
    printf("ladspa2MidiControlValues: has LADSPA_HINT_BOUNDED_BELOW\n");
    #endif
    
    fmin =  range.LowerBound * m;
  }  
  else
    fmin = 0.0;
  
  if(desc & LADSPA_HINT_BOUNDED_ABOVE)
  {  
    #ifdef PLUGIN_DEBUGIN 
    printf("ladspa2MidiControlValues: has LADSPA_HINT_BOUNDED_ABOVE\n");
    #endif
    
    fmax =  range.UpperBound * m;
  }  
  else
    fmax = 1.0;
    
  frng = fmax - fmin;
  imin = lrint(fmin);  
  imax = lrint(fmax);  
  //irng = imax - imin;

  int ctlmn = 0;
  int ctlmx = 127;
  
  #ifdef PLUGIN_DEBUGIN 
  printf("ladspa2MidiControlValues: port min:%f max:%f \n", fmin, fmax);
  #endif
  
  //bool isneg = (fmin < 0.0);
  bool isneg = (imin < 0);
  int bias = 0;
  switch(t) 
  {
    case MidiController::RPN:
    case MidiController::NRPN:
    case MidiController::Controller7:
      if(isneg)
      {
        ctlmn = -64;
        ctlmx = 63;
        bias = -64;
      }
      else
      {
        ctlmn = 0;
        ctlmx = 127;
      }
    break;
    case MidiController::Controller14:
    case MidiController::RPN14:
    case MidiController::NRPN14:
      if(isneg)
      {
        ctlmn = -8192;
        ctlmx = 8191;
        bias = -8192;
      }
      else
      {
        ctlmn = 0;
        ctlmx = 16383;
      }
    break;
    case MidiController::Program:
      ctlmn = 0;
      //ctlmx = 0xffffff;
      ctlmx = 0x3fff;     // FIXME: Really should not happen or be allowed. What to do here...
    break;
    case MidiController::Pitch:
      ctlmn = -8192;
      ctlmx = 8191;
    break;
    case MidiController::Velo:        // cannot happen
    default:
      break;
  }
  //int ctlrng = ctlmx - ctlmn;
  float fctlrng = float(ctlmx - ctlmn);
  
  // Is it an integer control?
  if(desc & LADSPA_HINT_INTEGER)
  {
    #ifdef PLUGIN_DEBUGIN 
    printf("ladspa2MidiControlValues: has LADSPA_HINT_INTEGER\n");
    #endif
  
    // If the upper or lower limit is beyond the controller limits, just scale the whole range to fit.
    // We could get fancy by scaling only the negative or positive domain, or each one separately, but no...
    //if((imin < ctlmn) || (imax > ctlmx))
    //{
    //  float scl = float(irng) / float(fctlrng);
    //  if((ctlmn - imin) > (ctlmx - imax))
    //    scl = float(ctlmn - imin);
    //  else
    //    scl = float(ctlmx - imax);
    //}
    // No, instead just clip the limits. ie fit the range into clipped space.
    if(imin < ctlmn)
      imin = ctlmn;
    if(imax > ctlmx)
      imax = ctlmx;
      
    *min = imin;
    *max = imax;
    
    //int idef = (int)lrint(fdef);
    //if(idef < ctlmn)
    //  idef = ctlmn;
    //if(idef > ctlmx)
    //  idef = ctlmx;
    //*def = idef;
    
    *def = (int)lrint(fdef);
    
    return hasdef;
  }
  
  // It's a floating point control, just use wide open maximum range.
  *min = ctlmn;
  *max = ctlmx;
  
  float fbias = (fmin + fmax) / 2.0;
  float normbias = fbias / frng;
  float normdef = fdef / frng;
  fdef = normdef * fctlrng;
  
  // FIXME: TODO: Incorrect... Fix this somewhat more trivial stuff later....
  
  *def = (int)lrint(fdef) + bias;
 
  #ifdef PLUGIN_DEBUGIN 
  printf("ladspa2MidiControlValues: setting default:%d\n", *def);
  #endif
  
  return hasdef;
}      

//---------------------------------------------------------
//   midi2LadspaValue
//---------------------------------------------------------

float midi2LadspaValue(const LADSPA_Descriptor* plugin, int port, int ctlnum, int val)
{
  LADSPA_PortRangeHint range = plugin->PortRangeHints[port];
  LADSPA_PortRangeHintDescriptor desc = range.HintDescriptor;
  
  float fmin, fmax;
  int   imin;
  //int imax;
  float frng;
  //int idef;
  
  //ladspaControlRange(plugin, port, &fmin, &fmax);
  
  //bool hasdef = ladspaDefaultValue(plugin, port, &fdef); 
  //bool isint = desc & LADSPA_HINT_INTEGER;
  MidiController::ControllerType t = midiControllerType(ctlnum);
  
  #ifdef PLUGIN_DEBUGIN 
  printf("midi2LadspaValue: ctlnum:%d ladspa port:%d val:%d\n", ctlnum, port, val);
  #endif
  
  float m = 1.0;
  if(desc & LADSPA_HINT_SAMPLE_RATE)
  {
    #ifdef PLUGIN_DEBUGIN 
    printf("midi2LadspaValue: has LADSPA_HINT_SAMPLE_RATE\n");
    #endif
    
    m = float(sampleRate);
  }  
  
  if(desc & LADSPA_HINT_BOUNDED_BELOW)
  {
    #ifdef PLUGIN_DEBUGIN 
    printf("midi2LadspaValue: has LADSPA_HINT_BOUNDED_BELOW\n");
    #endif
    
    fmin =  range.LowerBound * m;
  }  
  else
    fmin = 0.0;
  
  if(desc & LADSPA_HINT_BOUNDED_ABOVE)
  {  
    #ifdef PLUGIN_DEBUGIN 
    printf("midi2LadspaValue: has LADSPA_HINT_BOUNDED_ABOVE\n");
    #endif
    
    fmax =  range.UpperBound * m;
  }  
  else
    fmax = 1.0;
    
  frng = fmax - fmin;
  imin = lrint(fmin);  
  //imax = lrint(fmax);  
  //irng = imax - imin;

  if(desc & LADSPA_HINT_TOGGLED) 
  {
    #ifdef PLUGIN_DEBUGIN 
    printf("midi2LadspaValue: has LADSPA_HINT_TOGGLED\n");
    #endif
    
    if(val > 0)
      return fmax;
    else
      return fmin;
  }
  
  int ctlmn = 0;
  int ctlmx = 127;
  
  #ifdef PLUGIN_DEBUGIN 
  printf("midi2LadspaValue: port min:%f max:%f \n", fmin, fmax);
  #endif
  
  //bool isneg = (fmin < 0.0);
  bool isneg = (imin < 0);
  int bval = val;
  int cval = val;
  switch(t) 
  {
    case MidiController::RPN:
    case MidiController::NRPN:
    case MidiController::Controller7:
      if(isneg)
      {
        ctlmn = -64;
        ctlmx = 63;
        bval -= 64;
        cval -= 64;
      }
      else
      {
        ctlmn = 0;
        ctlmx = 127;
        cval -= 64;
      }
    break;
    case MidiController::Controller14:
    case MidiController::RPN14:
    case MidiController::NRPN14:
      if(isneg)
      {
        ctlmn = -8192;
        ctlmx = 8191;
        bval -= 8192;
        cval -= 8192;
      }
      else
      {
        ctlmn = 0;
        ctlmx = 16383;
        cval -= 8192;
      }
    break;
    case MidiController::Program:
      ctlmn = 0;
      ctlmx = 0xffffff;
    break;
    case MidiController::Pitch:
      ctlmn = -8192;
      ctlmx = 8191;
    break;
    case MidiController::Velo:        // cannot happen
    default:
      break;
  }
  int ctlrng = ctlmx - ctlmn;
  float fctlrng = float(ctlmx - ctlmn);
  
  // Is it an integer control?
  if(desc & LADSPA_HINT_INTEGER)
  {
    float ret = float(cval);
    if(ret < fmin)
      ret = fmin;
    if(ret > fmax)
      ret = fmax;
    #ifdef PLUGIN_DEBUGIN 
    printf("midi2LadspaValue: has LADSPA_HINT_INTEGER returning:%f\n", ret);
    #endif
    
    return ret;  
  }
  
  // Avoid divide-by-zero error below.
  if(ctlrng == 0)
    return 0.0;
    
  // It's a floating point control, just use wide open maximum range.
  float normval = float(bval) / fctlrng;
  //float fbias = (fmin + fmax) / 2.0;
  //float normfbias = fbias / frng;
  //float ret = (normdef + normbias) * fctlrng;
  //float normdef = fdef / frng;
  
  float ret = normval * frng + fmin;
  
  #ifdef PLUGIN_DEBUGIN 
  printf("midi2LadspaValue: float returning:%f\n", ret);
  #endif
  
  return ret;
}      


// Works but not needed.
/*
//---------------------------------------------------------
//   ladspa2MidiController
//---------------------------------------------------------

MidiController* ladspa2MidiController(const LADSPA_Descriptor* plugin, int port, int ctlnum)
{
  int min, max, def;
  
  if(!ladspa2MidiControlValues(plugin, port, ctlnum, &min, &max, &def))
    return 0;
  
  MidiController* mc = new MidiController(QString(plugin->PortNames[port]), ctlnum, min, max, def);
  
  return mc;
}
*/

//----------------------------------------------------------------------------------
//   defaultValue
//   If no default ladspa value found, still sets *def to 1.0, but returns false.
//---------------------------------------------------------------------------------

//float ladspaDefaultValue(const LADSPA_Descriptor* plugin, int k)
bool ladspaDefaultValue(const LADSPA_Descriptor* plugin, int port, float* val)
{
      LADSPA_PortRangeHint range = plugin->PortRangeHints[port];
      LADSPA_PortRangeHintDescriptor rh = range.HintDescriptor;
//      bool isLog = LADSPA_IS_HINT_LOGARITHMIC(rh);
      //double val = 1.0;
      float m = (rh & LADSPA_HINT_SAMPLE_RATE) ? float(sampleRate) : 1.0f;
      if (LADSPA_IS_HINT_DEFAULT_MINIMUM(rh)) 
      {
        *val = range.LowerBound * m;
        return true;
      }
      else if (LADSPA_IS_HINT_DEFAULT_LOW(rh)) 
      {
            if (LADSPA_IS_HINT_LOGARITHMIC(rh))
            {
              *val = exp(fast_log10(range.LowerBound * m) * .75 +
                     log(range.UpperBound * m) * .25);
              return true;
            }         
            else
            {
              *val = range.LowerBound*.75*m + range.UpperBound*.25*m;
              return true;
            }      
      }
      else if (LADSPA_IS_HINT_DEFAULT_MIDDLE(rh)) 
      {
            if (LADSPA_IS_HINT_LOGARITHMIC(rh))
            {
              *val = exp(log(range.LowerBound * m) * .5 +
                     log10(range.UpperBound * m) * .5);
              return true;
            }         
            else
            {
              *val = range.LowerBound*.5*m + range.UpperBound*.5*m;
              return true;
            }      
      }
      else if (LADSPA_IS_HINT_DEFAULT_HIGH(rh)) 
      {
            if (LADSPA_IS_HINT_LOGARITHMIC(rh))
            {
              *val = exp(log(range.LowerBound * m) * .25 +
                     log(range.UpperBound * m) * .75);
              return true;
            }         
            else
            {
              *val = range.LowerBound*.25*m + range.UpperBound*.75*m;
              return true;
            }      
      }
      else if (LADSPA_IS_HINT_DEFAULT_MAXIMUM(rh)) 
      {
            *val = range.UpperBound*m;
            return true;
      }
      else if (LADSPA_IS_HINT_DEFAULT_0(rh))
      {
            *val = 0.0;
            return true;
      }      
      else if (LADSPA_IS_HINT_DEFAULT_1(rh))
      {
            *val = 1.0;
            return true;
      }      
      else if (LADSPA_IS_HINT_DEFAULT_100(rh))
      {
            *val = 100.0;
            return true;
      }      
      else if (LADSPA_IS_HINT_DEFAULT_440(rh))
      {
            *val = 440.0;
            return true;
      }      
      
      // No default found. Set return value to 1.0, but return false.
      *val = 1.0;
      return false;
}

//---------------------------------------------------------
//   ladspaControlRange
//---------------------------------------------------------

void ladspaControlRange(const LADSPA_Descriptor* plugin, int i, float* min, float* max) 
      {
      LADSPA_PortRangeHint range = plugin->PortRangeHints[i];
      LADSPA_PortRangeHintDescriptor desc = range.HintDescriptor;
      if (desc & LADSPA_HINT_TOGGLED) {
            *min = 0.0;
            *max = 1.0;
            return;
            }
      float m = 1.0;
      if (desc & LADSPA_HINT_SAMPLE_RATE)
            m = float(sampleRate);

      if (desc & LADSPA_HINT_BOUNDED_BELOW)
            *min =  range.LowerBound * m;
      else
            *min = 0.0;
      if (desc & LADSPA_HINT_BOUNDED_ABOVE)
            *max =  range.UpperBound * m;
      else
            *max = 1.0;
      }

//---------------------------------------------------------
//   Plugin
//---------------------------------------------------------

Plugin::Plugin(QFileInfo* f, const LADSPA_Descriptor* d, bool isDssi)
{
  _isDssi = isDssi;
  #ifdef DSSI_SUPPORT
  dssi_descr = NULL;
  #endif
  
  fi = *f;
  plugin = NULL;
  ladspa = NULL;
  _handle = 0;
  _references = 0;
  _instNo     = 0;
  _label = QString(d->Label); 
  _name = QString(d->Name); 
  _uniqueID = d->UniqueID; 
  _maker = QString(d->Maker); 
  _copyright = QString(d->Copyright); 
  
  _portCount = d->PortCount;
  //_portDescriptors = 0;
  //if(_portCount)
  //  _portDescriptors = new LADSPA_PortDescriptor[_portCount];
  
  
  _inports = 0;
  _outports = 0;
  _controlInPorts = 0;
  _controlOutPorts = 0;
  for(unsigned long k = 0; k < _portCount; ++k) 
  {
    LADSPA_PortDescriptor pd = d->PortDescriptors[k];
    //_portDescriptors[k] = pd;
    if(pd & LADSPA_PORT_AUDIO)
    {
      if(pd & LADSPA_PORT_INPUT)
        ++_inports;
      else
      if(pd & LADSPA_PORT_OUTPUT)
        ++_outports;
    }    
    else
    if(pd & LADSPA_PORT_CONTROL)
    {
      if(pd & LADSPA_PORT_INPUT)
        ++_controlInPorts;
      else
      if(pd & LADSPA_PORT_OUTPUT)
        ++_controlOutPorts;
    }    
  }
  
  _inPlaceCapable = !LADSPA_IS_INPLACE_BROKEN(d->Properties);
  
  // By T356. Blacklist vst plugins in-place configurable for now. At one point they 
  //   were working with in-place here, but not now, and RJ also reported they weren't working.
  // Fixes problem with vst plugins not working or feeding back loudly.
  // I can only think of two things that made them stop working:
  // 1): I switched back from Jack-2 to Jack-1
  // 2): I changed winecfg audio to use Jack instead of ALSA.
  // Will test later...
  // Possibly the first one because under Mandriva2007.1 (Jack-1), no matter how hard I tried, 
  //  the same problem existed. It may have been when using Jack-2 with Mandriva2009 that they worked.
  // Apparently the plugins are lying about their in-place capability.
  // Quote:
  /* Property LADSPA_PROPERTY_INPLACE_BROKEN indicates that the plugin
    may cease to work correctly if the host elects to use the same data
    location for both input and output (see connect_port()). This
    should be avoided as enabling this flag makes it impossible for
    hosts to use the plugin to process audio `in-place.' */
  // Examination of all my ladspa and vst synths and effects plugins showed only one - 
  //  EnsembleLite (EnsLite VST) has the flag set, but it is a vst synth and is not involved here!
  // Yet many (all?) ladspa vst effect plugins exhibit this problem.  
  // Changed by Tim. p3.3.14
  if ((_inports != _outports) || (fi.baseName(true) == QString("dssi-vst") && !config.vstInPlace))
        _inPlaceCapable = false;
}

Plugin::~Plugin()
{
  //if(_portDescriptors)
  //  delete[] _portDescriptors;
}
  
//---------------------------------------------------------
//   incReferences
//---------------------------------------------------------

int Plugin::incReferences(int val)
{
  #ifdef PLUGIN_DEBUGIN 
  fprintf(stderr, "Plugin::incReferences _references:%d val:%d\n", _references, val);
  #endif
  
  int newref = _references + val;
  
  if(newref == 0) 
  {
    _references = 0;
    if(_handle)
    {
      #ifdef PLUGIN_DEBUGIN 
      fprintf(stderr, "Plugin::incReferences no more instances, closing library\n");
      #endif
      
      dlclose(_handle);
    }
    
    _handle = 0;
    ladspa = NULL;
    plugin = NULL;
    rpIdx.clear();
    
    #ifdef DSSI_SUPPORT
    dssi_descr = NULL;
    #endif
    
    return 0;
  }
    
  //if(_references == 0) 
  if(_handle == 0) 
  {
    //_references = 0;
    _handle = dlopen(fi.filePath().latin1(), RTLD_NOW);
    //handle = dlopen(fi.absFilePath().latin1(), RTLD_NOW);
    
    if(_handle == 0) 
    {
      fprintf(stderr, "Plugin::incReferences dlopen(%s) failed: %s\n",
              fi.filePath().latin1(), dlerror());
              //fi.absFilePath().latin1(), dlerror());
      return 0;
    }
    
    #ifdef DSSI_SUPPORT
    DSSI_Descriptor_Function dssi = (DSSI_Descriptor_Function)dlsym(_handle, "dssi_descriptor");
    if(dssi)
    {
      const DSSI_Descriptor* descr;
      for(int i = 0;; ++i) 
      {
        descr = dssi(i);
        if(descr == NULL)
          break;
        
        QString label(descr->LADSPA_Plugin->Label);
        // Listing effect plugins only while excluding synths:
        // Do exactly what dssi-vst.cpp does for listing ladspa plugins.
        //if(label == _name &&
        if(label == _label &&
          !descr->run_synth &&
          !descr->run_synth_adding &&
          !descr->run_multiple_synths &&
          !descr->run_multiple_synths_adding) 
        {  
          _isDssi = true;
          ladspa = NULL;
          dssi_descr = descr;
          plugin = descr->LADSPA_Plugin;
          break;
        }
      }  
    }
    else
    #endif // DSSI_SUPPORT   
    {
      LADSPA_Descriptor_Function ladspadf = (LADSPA_Descriptor_Function)dlsym(_handle, "ladspa_descriptor");
      if(ladspadf)
      {
        const LADSPA_Descriptor* descr;
        for(int i = 0;; ++i) 
        {
          descr = ladspadf(i);
          if(descr == NULL)
            break;
          
          QString label(descr->Label);
          //if(label == _name)
          if(label == _label)
          {  
            _isDssi = false;
            ladspa = ladspadf;
            plugin = descr;
            
            #ifdef DSSI_SUPPORT
            dssi_descr = NULL;
            #endif
            
            break;
          }
        }  
      }
    }    
    
    if(plugin != NULL)
    {
      //_instNo     = 0;
      _name = QString(plugin->Name); 
      _uniqueID = plugin->UniqueID; 
      _maker = QString(plugin->Maker); 
      _copyright = QString(plugin->Copyright); 
      
      //if(_portDescriptors)
      //  delete[] _portDescriptors;
      //_portDescriptors = 0;  
      _portCount = plugin->PortCount;
      //if(_portCount)
      //  _portDescriptors = new LADSPA_PortDescriptor[_portCount];
        
      _inports = 0;
      _outports = 0;
      _controlInPorts = 0;
      _controlOutPorts = 0;
      for(unsigned long k = 0; k < _portCount; ++k) 
      {
        LADSPA_PortDescriptor pd = plugin->PortDescriptors[k];
        //_portDescriptors[k] = pd;
        if(pd & LADSPA_PORT_AUDIO)
        {
          if(pd & LADSPA_PORT_INPUT)
            ++_inports;
          else
          if(pd & LADSPA_PORT_OUTPUT)
            ++_outports;
          
          rpIdx.push_back((unsigned long)-1);
        }    
        else
        if(pd & LADSPA_PORT_CONTROL)
        {
          if(pd & LADSPA_PORT_INPUT)
          {
            rpIdx.push_back(_controlInPorts);
            ++_controlInPorts;
          }  
          else
          if(pd & LADSPA_PORT_OUTPUT)
          {
            rpIdx.push_back((unsigned long)-1);
            ++_controlOutPorts;
          }  
        }    
      }
      
      _inPlaceCapable = !LADSPA_IS_INPLACE_BROKEN(plugin->Properties);
      
      // Blacklist vst plugins in-place configurable for now. 
      if ((_inports != _outports) || (fi.baseName(true) == QString("dssi-vst") && !config.vstInPlace))
            _inPlaceCapable = false;
    }
  }      
        
  if(plugin == NULL)
  {
    dlclose(_handle);
    _handle = 0;
    _references = 0;
    fprintf(stderr, "Plugin::incReferences Error: %s no plugin!\n", fi.filePath().latin1()); 
    return 0;
  }
        
  _references = newref;
  
  //QString guiPath(info.dirPath() + "/" + info.baseName());
  //QDir guiDir(guiPath, "*", QDir::Unsorted, QDir::Files);
  //_hasGui = guiDir.exists();
  
  return _references;
}

//---------------------------------------------------------
//   range
//---------------------------------------------------------

void Plugin::range(unsigned long i, float* min, float* max) const
      {
      LADSPA_PortRangeHint range = plugin->PortRangeHints[i];
      LADSPA_PortRangeHintDescriptor desc = range.HintDescriptor;
      if (desc & LADSPA_HINT_TOGGLED) {
            *min = 0.0;
            *max = 1.0;
            return;
            }
      float m = 1.0;
      if (desc & LADSPA_HINT_SAMPLE_RATE)
            m = float(sampleRate);

      if (desc & LADSPA_HINT_BOUNDED_BELOW)
            *min =  range.LowerBound * m;
      else
            *min = 0.0;
      if (desc & LADSPA_HINT_BOUNDED_ABOVE)
            *max =  range.UpperBound * m;
      else
            *max = 1.0;
      }

//---------------------------------------------------------
//   defaultValue
//---------------------------------------------------------

double Plugin::defaultValue(unsigned long port) const
{
    if(port >= plugin->PortCount) 
      return 0.0;
      
    LADSPA_PortRangeHint range = plugin->PortRangeHints[port];
    LADSPA_PortRangeHintDescriptor rh = range.HintDescriptor;
    double val = 1.0;
    if (LADSPA_IS_HINT_DEFAULT_MINIMUM(rh))
          val = range.LowerBound;
    else if (LADSPA_IS_HINT_DEFAULT_LOW(rh))
          if (LADSPA_IS_HINT_LOGARITHMIC(range.HintDescriptor))
                val = exp(fast_log10(range.LowerBound) * .75 +
                    log(range.UpperBound) * .25);
          else
                val = range.LowerBound*.75 + range.UpperBound*.25;
    else if (LADSPA_IS_HINT_DEFAULT_MIDDLE(rh))
          if (LADSPA_IS_HINT_LOGARITHMIC(range.HintDescriptor))
                val = exp(log(range.LowerBound) * .5 +
                    log(range.UpperBound) * .5);
          else
                val = range.LowerBound*.5 + range.UpperBound*.5;
    else if (LADSPA_IS_HINT_DEFAULT_HIGH(rh))
          if (LADSPA_IS_HINT_LOGARITHMIC(range.HintDescriptor))
                val = exp(log(range.LowerBound) * .25 +
                    log(range.UpperBound) * .75);
          else
                val = range.LowerBound*.25 + range.UpperBound*.75;
    else if (LADSPA_IS_HINT_DEFAULT_MAXIMUM(rh))
          val = range.UpperBound;
    else if (LADSPA_IS_HINT_DEFAULT_0(rh))
          val = 0.0;
    else if (LADSPA_IS_HINT_DEFAULT_1(rh))
          val = 1.0;
    else if (LADSPA_IS_HINT_DEFAULT_100(rh))
          val = 100.0;
    else if (LADSPA_IS_HINT_DEFAULT_440(rh))
          val = 440.0;
          
    return val;      
}

//---------------------------------------------------------
//   loadPluginLib
//---------------------------------------------------------

static void loadPluginLib(QFileInfo* fi)
{
  void* handle = dlopen(fi->filePath().ascii(), RTLD_NOW);
  if (handle == 0) {
        fprintf(stderr, "dlopen(%s) failed: %s\n",
          fi->filePath().ascii(), dlerror());
        return;
        }

  #ifdef DSSI_SUPPORT
  DSSI_Descriptor_Function dssi = (DSSI_Descriptor_Function)dlsym(handle, "dssi_descriptor");
  if(dssi)
  {
    const DSSI_Descriptor* descr;
    for (int i = 0;; ++i) 
    {
      descr = dssi(i);
      if (descr == 0)
            break;
      
      // Listing effect plugins only while excluding synths:
      // Do exactly what dssi-vst.cpp does for listing ladspa plugins.
      if(!descr->run_synth &&
        !descr->run_synth_adding &&
        !descr->run_multiple_synths &&
        !descr->run_multiple_synths_adding) 
      {
        // Make sure it doesn't already exist.
        if(plugins.find(fi->baseName(true), QString(descr->LADSPA_Plugin->Label)) != 0)
          continue;
        
        #ifdef PLUGIN_DEBUGIN 
        fprintf(stderr, "loadPluginLib: dssi effect name:%s inPlaceBroken:%d\n", descr->LADSPA_Plugin->Name, LADSPA_IS_INPLACE_BROKEN(descr->LADSPA_Plugin->Properties));
        #endif
      
        //LADSPA_Properties properties = descr->LADSPA_Plugin->Properties;
        //bool inPlaceBroken = LADSPA_IS_INPLACE_BROKEN(properties);
        //plugins.add(fi, descr, !inPlaceBroken);
        plugins.add(fi, descr->LADSPA_Plugin, true);
      }
    }      
  }
  else
  #endif
  {
    LADSPA_Descriptor_Function ladspa = (LADSPA_Descriptor_Function)dlsym(handle, "ladspa_descriptor");
    if(!ladspa) 
    {
      const char *txt = dlerror();
      if(txt) 
      {
        fprintf(stderr,
              "Unable to find ladspa_descriptor() function in plugin "
              "library file \"%s\": %s.\n"
              "Are you sure this is a LADSPA plugin file?\n",
              fi->filePath().ascii(),
              txt);
      }
      dlclose(handle);
      return;
    }
    
    const LADSPA_Descriptor* descr;
    for (int i = 0;; ++i) 
    {
      descr = ladspa(i);
      if (descr == NULL)
            break;
      
      // Make sure it doesn't already exist.
      if(plugins.find(fi->baseName(true), QString(descr->Label)) != 0)
        continue;
      
      #ifdef PLUGIN_DEBUGIN 
      fprintf(stderr, "loadPluginLib: ladspa effect name:%s inPlaceBroken:%d\n", descr->Name, LADSPA_IS_INPLACE_BROKEN(descr->Properties));
      #endif
      
      //LADSPA_Properties properties = descr->Properties;
      //bool inPlaceBroken = LADSPA_IS_INPLACE_BROKEN(properties);
      //plugins.add(fi, ladspa, descr, !inPlaceBroken);
      plugins.add(fi, descr);
    }
  }  
  
  dlclose(handle);
}

//---------------------------------------------------------
//   loadPluginDir
//---------------------------------------------------------

static void loadPluginDir(const QString& s)
      {
      if (debugMsg)
            printf("scan ladspa plugin dir <%s>\n", s.latin1());
      QDir pluginDir(s, QString("*.so")); // ddskrjo
      if (pluginDir.exists()) {
            QFileInfoList list = pluginDir.entryInfoList();
            QFileInfoListIterator it=list.begin();
            while(it != list.end()) {
                  loadPluginLib(&*it);
                  ++it;
                  }
            }
      }

//---------------------------------------------------------
//   initPlugins
//---------------------------------------------------------

void initPlugins()
      {
      loadPluginDir(museGlobalLib + QString("/plugins"));

      const char* p = 0;
      
      // Take care of DSSI plugins first...
      #ifdef DSSI_SUPPORT
      const char* dssiPath = getenv("DSSI_PATH");
      if (dssiPath == 0)
            dssiPath = "/usr/local/lib64/dssi:/usr/lib64/dssi:/usr/local/lib/dssi:/usr/lib/dssi";
      p = dssiPath;
      while (*p != '\0') {
            const char* pe = p;
            while (*pe != ':' && *pe != '\0')
                  pe++;

            int n = pe - p;
            if (n) {
                  char* buffer = new char[n + 1];
                  strncpy(buffer, p, n);
                  buffer[n] = '\0';
                  loadPluginDir(QString(buffer));
                  delete[] buffer;
                  }
            p = pe;
            if (*p == ':')
                  p++;
            }
      #endif
      
      // Now do LADSPA plugins...
      const char* ladspaPath = getenv("LADSPA_PATH");
      if (ladspaPath == 0)
            ladspaPath = "/usr/local/lib64/ladspa:/usr/lib64/ladspa:/usr/local/lib/ladspa:/usr/lib/ladspa";
      p = ladspaPath;
      
      if(debugMsg)
        fprintf(stderr, "loadPluginLib: ladspa path:%s\n", ladspaPath);
      
      while (*p != '\0') {
            const char* pe = p;
            while (*pe != ':' && *pe != '\0')
                  pe++;

            int n = pe - p;
            if (n) {
                  char* buffer = new char[n + 1];
                  strncpy(buffer, p, n);
                  buffer[n] = '\0';
                  if(debugMsg)
                    fprintf(stderr, "loadPluginLib: loading ladspa dir:%s\n", buffer);
                  
                  loadPluginDir(QString(buffer));
                  delete[] buffer;
                  }
            p = pe;
            if (*p == ':')
                  p++;
            }
      }

//---------------------------------------------------------
//   find
//---------------------------------------------------------

Plugin* PluginList::find(const QString& file, const QString& name)
      {
      for (iPlugin i = begin(); i != end(); ++i) {
            if ((file == i->lib()) && (name == i->label()))
                  return &*i;
            }
      //printf("Plugin <%s> not found\n", name.ascii());
      return 0;
      }

//---------------------------------------------------------
//   Pipeline
//---------------------------------------------------------

Pipeline::Pipeline()
   : std::vector<PluginI*>()
      {
      // Added by Tim. p3.3.15
      for (int i = 0; i < MAX_CHANNELS; ++i)
            posix_memalign((void**)(buffer + i), 16, sizeof(float) * segmentSize);
      
      for (int i = 0; i < PipelineDepth; ++i)
            push_back(0);
      }

//---------------------------------------------------------
//   ~Pipeline
//---------------------------------------------------------

Pipeline::~Pipeline()
      {
      removeAll();
      for (int i = 0; i < MAX_CHANNELS; ++i)
            ::free(buffer[i]);
      }

//---------------------------------------------------------
//   setChannels
//---------------------------------------------------------

void Pipeline::setChannels(int n)
      {
      for (int i = 0; i < PipelineDepth; ++i)
            if ((*this)[i])
                  (*this)[i]->setChannels(n);
      }

//---------------------------------------------------------
//   insert
//    give ownership of object plugin to Pipeline
//---------------------------------------------------------

void Pipeline::insert(PluginI* plugin, int index)
      {
      remove(index);
      (*this)[index] = plugin;
      }

//---------------------------------------------------------
//   remove
//---------------------------------------------------------

void Pipeline::remove(int index)
      {
      PluginI* plugin = (*this)[index];
      if (plugin)
            delete plugin;
      (*this)[index] = 0;
      }

//---------------------------------------------------------
//   removeAll
//---------------------------------------------------------

void Pipeline::removeAll()
      {
      for (int i = 0; i < PipelineDepth; ++i)
            remove(i);
      }

//---------------------------------------------------------
//   isOn
//---------------------------------------------------------

bool Pipeline::isOn(int idx) const
      {
      PluginI* p = (*this)[idx];
      if (p)
            return p->on();
      return false;
      }

//---------------------------------------------------------
//   setOn
//---------------------------------------------------------

void Pipeline::setOn(int idx, bool flag)
      {
      PluginI* p = (*this)[idx];
      if (p) {
            p->setOn(flag);
            if (p->gui())
                  p->gui()->setOn(flag);
            }
      }

//---------------------------------------------------------
//   label
//---------------------------------------------------------

QString Pipeline::label(int idx) const
      {
      PluginI* p = (*this)[idx];
      if (p)
            return p->label();
      return QString("");
      }

//---------------------------------------------------------
//   name
//---------------------------------------------------------

QString Pipeline::name(int idx) const
      {
      PluginI* p = (*this)[idx];
      if (p)
            return p->name();
      return QString("empty");
      }

//---------------------------------------------------------
//   empty
//---------------------------------------------------------

bool Pipeline::empty(int idx) const
      {
      PluginI* p = (*this)[idx];
      return p == 0;
      }

//---------------------------------------------------------
//   move
//---------------------------------------------------------

void Pipeline::move(int idx, bool up)
{
      PluginI* p1 = (*this)[idx];
      if (up) 
      {
            (*this)[idx]   = (*this)[idx-1];
          
          if((*this)[idx])
            (*this)[idx]->setID(idx);
            
            (*this)[idx-1] = p1;
          
          if(p1)
          {
            p1->setID(idx - 1);
            if(p1->track())
              audio->msgSwapControllerIDX(p1->track(), idx, idx - 1);
            }
      }
      else 
      {
            (*this)[idx]   = (*this)[idx+1];
          
          if((*this)[idx])
            (*this)[idx]->setID(idx);
          
            (*this)[idx+1] = p1;
          
          if(p1)
          {
            p1->setID(idx + 1);
            if(p1->track())
              audio->msgSwapControllerIDX(p1->track(), idx, idx + 1);
            }
      }
}

//---------------------------------------------------------
//   isDssiPlugin
//---------------------------------------------------------

bool Pipeline::isDssiPlugin(int idx) const
{
  PluginI* p = (*this)[idx];
  if(p)
    return p->isDssiPlugin();
        
  return false;               
}

//---------------------------------------------------------
//   showGui
//---------------------------------------------------------

void Pipeline::showGui(int idx, bool flag)
      {
      PluginI* p = (*this)[idx];
      if (p)
            p->showGui(flag);
      }

//---------------------------------------------------------
//   showNativeGui
//---------------------------------------------------------

void Pipeline::showNativeGui(int idx, bool flag)
      {
      #ifdef OSC_SUPPORT
      PluginI* p = (*this)[idx];
      if (p)
            p->oscIF().oscShowGui(flag);
      #endif      
      }

//---------------------------------------------------------
//   deleteGui
//---------------------------------------------------------

void Pipeline::deleteGui(int idx)
{
  if(idx >= PipelineDepth)
    return;
  PluginI* p = (*this)[idx];
  if(p)
    p->deleteGui();
}

//---------------------------------------------------------
//   deleteAllGuis
//---------------------------------------------------------

void Pipeline::deleteAllGuis()
{
  for(int i = 0; i < PipelineDepth; i++)
    deleteGui(i);
}

//---------------------------------------------------------
//   guiVisible
//---------------------------------------------------------

bool Pipeline::guiVisible(int idx)
      {
      PluginI* p = (*this)[idx];
      if (p)
            return p->guiVisible();
      return false;
      }

//---------------------------------------------------------
//   nativeGuiVisible
//---------------------------------------------------------

bool Pipeline::nativeGuiVisible(int idx)
      {
      PluginI* p = (*this)[idx];
      if (p)
            return p->nativeGuiVisible();
      return false;
      }

//---------------------------------------------------------
//   apply
//---------------------------------------------------------

void Pipeline::apply(int ports, unsigned long nframes, float** buffer1)
{
      // prepare a second set of buffers in case a plugin is not
      // capable of inPlace processing

      // Removed by Tim. p3.3.15
      //float* buffer2[ports];
      //float data[nframes * ports];
      //for (int i = 0; i < ports; ++i)
      //      buffer2[i] = data + i * nframes;

      // p3.3.41
      //fprintf(stderr, "Pipeline::apply data: nframes:%ld %e %e %e %e\n", nframes, buffer1[0][0], buffer1[0][1], buffer1[0][2], buffer1[0][3]);
      
      bool swap = false;

      for (iPluginI ip = begin(); ip != end(); ++ip) {
            PluginI* p = *ip;
            if (p && p->on()) {
                  if (p->inPlaceCapable()) 
                  {
                        if (swap)
                              //p->connect(ports, buffer2, buffer2);
                              p->connect(ports, buffer, buffer);
                        else
                              p->connect(ports, buffer1, buffer1);
                  }
                  else 
                  {
                        if (swap)
                              //p->connect(ports, buffer2, buffer1);
                              p->connect(ports, buffer, buffer1);
                        else
                              //p->connect(ports, buffer1, buffer2);
                              p->connect(ports, buffer1, buffer);
                        swap = !swap;
                  }
                  p->apply(nframes);
                  }
            }
      if (swap) 
      {
            for (int i = 0; i < ports; ++i)
                  //memcpy(buffer1[i], buffer2[i], sizeof(float) * nframes);
                  //memcpy(buffer1[i], buffer[i], sizeof(float) * nframes);
                  AL::dsp->cpy(buffer1[i], buffer[i], nframes);
      }
      
      // p3.3.41
      //fprintf(stderr, "Pipeline::apply after data: nframes:%ld %e %e %e %e\n", nframes, buffer1[0][0], buffer1[0][1], buffer1[0][2], buffer1[0][3]);
      
}

//---------------------------------------------------------
//   PluginI
//---------------------------------------------------------

void PluginI::init()
      {
      _plugin           = 0;
      instances         = 0;
      handle            = 0;
      controls          = 0;
      controlsOut       = 0;
      controlPorts      = 0;
      controlOutPorts   = 0;
      _gui              = 0;
      _on               = true;
      initControlValues = false;
      _showNativeGuiPending = false;
      }

PluginI::PluginI()
      {
      _id = -1;
      _track = 0;
      
      init();
      }

//---------------------------------------------------------
//   PluginI
//---------------------------------------------------------

PluginI::~PluginI()
      {
      if (_plugin) {
            deactivate();
            _plugin->incReferences(-1);
            }
      if (_gui)
            delete _gui;
      if (controlsOut)
            delete[] controlsOut;
      if (controls)
            delete[] controls;
      if (handle)
            delete[] handle;
      }

//---------------------------------------------------------
//   setID
//---------------------------------------------------------

void PluginI::setID(int i)
{
  _id = i; 
}

//---------------------------------------------------------
//   updateControllers
//---------------------------------------------------------

void PluginI::updateControllers()
{
  if(!_track)
    return;
    
  for(int i = 0; i < controlPorts; ++i) 
    //audio->msgSetPluginCtrlVal(this, genACnum(_id, i), controls[i].val);
    // p3.3.43
    audio->msgSetPluginCtrlVal(_track, genACnum(_id, i), controls[i].val);
}
  
//---------------------------------------------------------
//   valueType
//---------------------------------------------------------

CtrlValueType PluginI::valueType() const
      {
      return VAL_LINEAR;
      }

//---------------------------------------------------------
//   setChannel
//---------------------------------------------------------

void PluginI::setChannels(int c)
{
      // p3.3.41 Removed
      //if (channel == c)
      //      return;
      
      // p3.3.41
      channel = c;
      
      //int ni = c / _plugin->outports();
      //if (ni == 0)
      //      ni = 1;
      // p3.3.41 Some plugins have zero out ports, causing exception with the above line.
      // Also, pick the least number of ins or outs, and base the number of instances on that.
      unsigned long ins = _plugin->inports();
      unsigned long outs = _plugin->outports();
      /*
      unsigned long minports = ~0ul;
      if(outs && outs < minports)
        minports = outs;
      if(ins && ins < minports)
        minports = ins;
      if(minports == ~0ul)
        minports = 1;
      int ni = c / minports;
      */
      int ni = 1;
      if(outs)
        ni = c / outs;
      else
      if(ins)
        ni = c / ins;
      
      if(ni < 1)
        ni = 1;
      
      if (ni == instances)
            return;
      
      // p3.3.41 Moved above.
      //channel = c;

      // remove old instances:
      deactivate();
      delete[] handle;
      instances = ni;
      handle    = new LADSPA_Handle[instances];
      for (int i = 0; i < instances; ++i) {
            handle[i] = _plugin->instantiate();
            if (handle[i] == NULL) {
                  printf("cannot instantiate instance %d\n", i);
                  return;
                  }
            }
      
      int curPort = 0;
      int curOutPort = 0;
      unsigned long ports   = _plugin->ports();
      for (unsigned long k = 0; k < ports; ++k) 
      {
            LADSPA_PortDescriptor pd = _plugin->portd(k);
            if (pd & LADSPA_PORT_CONTROL) 
            {
                  if(pd & LADSPA_PORT_INPUT) 
                  {
                    for (int i = 0; i < instances; ++i)
                          _plugin->connectPort(handle[i], k, &controls[curPort].val);
                    controls[curPort].idx = k;
                    ++curPort;
                  }
                  else  
                  if(pd & LADSPA_PORT_OUTPUT) 
                  {
                    for (int i = 0; i < instances; ++i)
                          _plugin->connectPort(handle[i], k, &controlsOut[curOutPort].val);
                    controlsOut[curOutPort].idx = k;
                    ++curOutPort;
                  }
            }
      }
      
      activate();
}

//---------------------------------------------------------
//   defaultValue
//---------------------------------------------------------

double PluginI::defaultValue(unsigned int param) const
{
//#warning controlPorts should really be unsigned
  if(param >= (unsigned)controlPorts)
    return 0.0;
  
  return _plugin->defaultValue(controls[param].idx);
}

LADSPA_Handle Plugin::instantiate() 
{
  LADSPA_Handle h = plugin->instantiate(plugin, sampleRate);
  if(h == NULL)
  {
    fprintf(stderr, "Plugin::instantiate() Error: plugin:%s instantiate failed!\n", plugin->Label); 
    return NULL;
  }
  
  //QString guiPath(info.dirPath() + "/" + info.baseName());
  //QDir guiDir(guiPath, "*", QDir::Unsorted, QDir::Files);
  //_hasGui = guiDir.exists();
  
  return h;
}

//---------------------------------------------------------
//   initPluginInstance
//    return true on error
//---------------------------------------------------------

bool PluginI::initPluginInstance(Plugin* plug, int c)
      {
      channel = c;
      if(plug == 0) 
      {
        printf("initPluginInstance: zero plugin\n");
        return true;
      }
      _plugin = plug;
      
      _plugin->incReferences(1);

      #ifdef OSC_SUPPORT
      _oscif.oscSetPluginI(this);      
      #endif
      
      QString inst("-" + QString::number(_plugin->instNo()));
      _name  = _plugin->name() + inst;
      _label = _plugin->label() + inst;

      //instances = channel/plug->outports();
      // p3.3.41 Some plugins have zero out ports, causing exception with the above line.
      // Also, pick the least number of ins or outs, and base the number of instances on that.
      unsigned long ins = plug->inports();
      unsigned long outs = plug->outports();
      /*
      unsigned long minports = ~0ul;
      if(outs && outs < minports)
        minports = outs;
      if(ins && ins < minports)
        minports = ins;
      if(minports == ~0ul)
        minports = 1;
      instances = channel / minports;
      if(instances < 1)
        instances = 1;
      */  
      if(outs)
      {
        instances = channel / outs;
        if(instances < 1)
          instances = 1;
      }
      else
      if(ins)
      {
        instances = channel / ins;
        if(instances < 1)
          instances = 1;
      }
      else
        instances = 1;
        
      handle = new LADSPA_Handle[instances];
      for(int i = 0; i < instances; ++i) 
      {
        #ifdef PLUGIN_DEBUGIN 
        fprintf(stderr, "PluginI::initPluginInstance instance:%d\n", i);
        #endif
        
        handle[i] = _plugin->instantiate();
        //if (handle[i] == 0)
        if(handle[i] == NULL)
          return true;
      }

      unsigned long ports = _plugin->ports();
      
      controlPorts = 0;
      controlOutPorts = 0;
      
      for(unsigned long k = 0; k < ports; ++k) 
      {
        LADSPA_PortDescriptor pd = _plugin->portd(k);
        if(pd & LADSPA_PORT_CONTROL)
        {
          if(pd & LADSPA_PORT_INPUT)
            ++controlPorts;
          else    
          if(pd & LADSPA_PORT_OUTPUT)
            ++controlOutPorts;
        }      
      }
      
      controls    = new Port[controlPorts];
      controlsOut = new Port[controlOutPorts];
      
      int i  = 0;
      int ii = 0;
      for(unsigned long k = 0; k < ports; ++k) 
      {
        LADSPA_PortDescriptor pd = _plugin->portd(k);
        if(pd & LADSPA_PORT_CONTROL) 
        {
          if(pd & LADSPA_PORT_INPUT)
          {
            double val = _plugin->defaultValue(k);
            controls[i].val    = val;
            controls[i].tmpVal = val;
            controls[i].enCtrl  = true;
            controls[i].en2Ctrl = true;
            ++i;
          }
          else
          if(pd & LADSPA_PORT_OUTPUT)
          {
            //double val = _plugin->defaultValue(k);
            controlsOut[ii].val     = 0.0;
            controlsOut[ii].tmpVal  = 0.0;
            controlsOut[ii].enCtrl  = false;
            controlsOut[ii].en2Ctrl = false;
            ++ii;
          }
        }
      }
      unsigned long curPort = 0;
      unsigned long curOutPort = 0;
      for(unsigned long k = 0; k < ports; ++k) 
      {
        LADSPA_PortDescriptor pd = _plugin->portd(k);
        if(pd & LADSPA_PORT_CONTROL) 
        {
          if(pd & LADSPA_PORT_INPUT)
          {
            for(int i = 0; i < instances; ++i)
              _plugin->connectPort(handle[i], k, &controls[curPort].val);
            controls[curPort].idx = k;
            ++curPort;
          }
          else
          if(pd & LADSPA_PORT_OUTPUT)
          {
            for(int i = 0; i < instances; ++i)
              _plugin->connectPort(handle[i], k, &controlsOut[curOutPort].val);
            controlsOut[curOutPort].idx = k;
            ++curOutPort;
          }
        }
      }
      activate();
      return false;
      }

//---------------------------------------------------------
//   connect
//---------------------------------------------------------

void PluginI::connect(int ports, float** src, float** dst)
      {
      int port = 0;
      for (int i = 0; i < instances; ++i) {
            for (unsigned long k = 0; k < _plugin->ports(); ++k) {
                  if (isAudioIn(k)) {
                        _plugin->connectPort(handle[i], k, src[port]);
                        port = (port + 1) % ports;
                        }
                  }
            }
      port = 0;
      for (int i = 0; i < instances; ++i) {
            for (unsigned long k = 0; k < _plugin->ports(); ++k) {
                  if (isAudioOut(k)) {
                        _plugin->connectPort(handle[i], k, dst[port]);
                        port = (port + 1) % ports;  // overwrite output?
//                        ++port;
//                        if (port >= ports) {
//                              return;
//                              }
                        }
                  }
            }
      }

//---------------------------------------------------------
//   deactivate
//---------------------------------------------------------

void PluginI::deactivate()
      {
      for (int i = 0; i < instances; ++i) {
            _plugin->deactivate(handle[i]);
            _plugin->cleanup(handle[i]);
            }
      }

//---------------------------------------------------------
//   activate
//---------------------------------------------------------

void PluginI::activate()
      {
      for (int i = 0; i < instances; ++i)
            _plugin->activate(handle[i]);
      if (initControlValues) {
            for (int i = 0; i < controlPorts; ++i) {
                  controls[i].val = controls[i].tmpVal;
                  }
            }
      else {
            //
            // get initial control values from plugin
            //
            for (int i = 0; i < controlPorts; ++i) {
                  controls[i].tmpVal = controls[i].val;
                  }
            }
      }

//---------------------------------------------------------
//   setControl
//    set plugin instance controller value by name
//---------------------------------------------------------

bool PluginI::setControl(const QString& s, double val)
      {
      for (int i = 0; i < controlPorts; ++i) {
            if (_plugin->portName(controls[i].idx) == s) {
                  controls[i].val = controls[i].tmpVal = val;
                  return false;
                  }
            }
      printf("PluginI:setControl(%s, %f) controller not found\n",
         s.latin1(), val);
      return true;
      }

//---------------------------------------------------------
//   saveConfiguration
//---------------------------------------------------------

void PluginI::writeConfiguration(int level, Xml& xml)
      {
      xml.tag(level++, "plugin file=\"%s\" label=\"%s\" channel=\"%d\"",
         //_plugin->lib().latin1(), _plugin->label().latin1(), instances * _plugin->inports());
         // p3.3.41
         //_plugin->lib().latin1(), _plugin->label().latin1(), channel);
         Xml::xmlString(_plugin->lib()).latin1(), Xml::xmlString(_plugin->label()).latin1(), channel);
         
      for (int i = 0; i < controlPorts; ++i) {
            int idx = controls[i].idx;
            QString s("control name=\"%1\" val=\"%2\" /");
            //xml.tag(level, s.arg(_plugin->portName(idx)).arg(controls[i].tmpVal).latin1());
            xml.tag(level, s.arg(Xml::xmlString(_plugin->portName(idx)).latin1()).arg(controls[i].tmpVal).latin1());
            }
      if (_on == false)
            xml.intTag(level, "on", _on);
      if (guiVisible()) {
            xml.intTag(level, "gui", 1);
            xml.geometryTag(level, "geometry", _gui);
            }
      if (nativeGuiVisible()) {
            xml.intTag(level, "nativegui", 1);
            // TODO:
            //xml.geometryTag(level, "nativegeometry", ?);
            }
      xml.tag(level--, "/plugin");
      }

//---------------------------------------------------------
//   loadControl
//---------------------------------------------------------

bool PluginI::loadControl(Xml& xml)
      {
      QString file;
      QString label;
      QString name("mops");
      double val = 0.0;

      for (;;) {
            Xml::Token token = xml.parse();
            const QString& tag = xml.s1();

            switch (token) {
                  case Xml::Error:
                  case Xml::End:
                        return true;
                  case Xml::TagStart:
                        xml.unknown("PluginI-Control");
                        break;
                  case Xml::Attribut:
                        if (tag == "name")
                              name = xml.s2();
                        else if (tag == "val")
                              val = xml.s2().toDouble();
                        break;
                  case Xml::TagEnd:
                        if (tag == "control") {
                              if (setControl(name, val)) {
                                    return false;
                                    }
                              initControlValues = true;
                              }
                        return true;
                  default:
                        break;
                  }
            }
      return true;
      }

//---------------------------------------------------------
//   readConfiguration
//    return true on error
//---------------------------------------------------------

bool PluginI::readConfiguration(Xml& xml, bool readPreset)
      {
      QString file;
      QString label;
      if (!readPreset)
            //instances = 1;
            // p3.3.41
            channel = 1;

      for (;;) {
            Xml::Token token(xml.parse());
            const QString& tag(xml.s1());
            switch (token) {
                  case Xml::Error:
                  case Xml::End:
                        return true;
                  case Xml::TagStart:
                        if (!readPreset && _plugin == 0) {
                              _plugin = plugins.find(file, label);
                              
                              //if (_plugin && initPluginInstance(_plugin, instances)) {
                              // p3.3.41
                              if (_plugin && initPluginInstance(_plugin, channel)) {
                                    _plugin = 0;
                                    xml.parse1();
                                    break;
                                    }
                              }
                        if (tag == "control")
                              loadControl(xml);
                        else if (tag == "on") {
                              bool flag = xml.parseInt();
                              if (!readPreset)
                                    _on = flag;
                              }
                        else if (tag == "gui") {
                              bool flag = xml.parseInt();
                              showGui(flag);
                              }
                        else if (tag == "nativegui") {
                              // We can't tell OSC to show the native plugin gui 
                              //  until the parent track is added to the lists.
                              // OSC needs to find the plugin in the track lists.
                              // Use this 'pending' flag so it gets done later.
                              _showNativeGuiPending = xml.parseInt();
                              }
                        else if (tag == "geometry") {
                              QRect r(readGeometry(xml, tag));
                              if (_gui) {
                                    _gui->resize(r.size());
                                    _gui->move(r.topLeft());
                                    }
                              }
                        else
                              xml.unknown("PluginI");
                        break;
                  case Xml::Attribut:
                        if (tag == "file") {
                              QString s = xml.s2();
                              if (readPreset) {
                                    if (s != plugin()->lib()) {
                                          printf("Error: Wrong preset type %s. Type must be a %s\n",
                                             s.latin1(), plugin()->lib().latin1());
                                          return true;
                                          }
                                    }
                              else {
                                    file = s;
                                    }
                              }
                        else if (tag == "label") {
                              if (!readPreset)
                                    label = xml.s2();
                              }
                        else if (tag == "channel") {
                              if (!readPreset)
                                    //instances = xml.s2().toInt();
                                    // p3.3.41
                                    channel = xml.s2().toInt();
                              }
                        break;
                  case Xml::TagEnd:
                        if (tag == "plugin") {
                              if (!readPreset && _plugin == 0) {
                                    _plugin = plugins.find(file, label);
                                    if (_plugin == 0)
                                          return true;
                                    
                                    //if (initPluginInstance(_plugin, instances))
                                    // p3.3.41
                                    if (initPluginInstance(_plugin, channel))
                                          return true;
                                    }
                              if (_gui)
                                    _gui->updateValues();
                              return false;
                              }
                        return true;
                  default:
                        break;
                  }
            }
      return true;
      }

//---------------------------------------------------------
//   showGui
//---------------------------------------------------------

void PluginI::showGui()
      {
      if (_plugin) {
            if (_gui == 0)
                    makeGui();
            if (_gui->isVisible())
                    _gui->hide();
            else
                    _gui->show();
            }
      }

void PluginI::showGui(bool flag)
      {
      if (_plugin) {
            if (flag) {
                    if (_gui == 0)
                        makeGui();
                    _gui->show();
                    }
            else {
                    if (_gui)
                        _gui->hide();
                    }
            }
      }

//---------------------------------------------------------
//   guiVisible
//---------------------------------------------------------

bool PluginI::guiVisible()
      {
      return _gui && _gui->isVisible();
      }

//---------------------------------------------------------
//   showNativeGui
//---------------------------------------------------------

void PluginI::showNativeGui()
{
  #ifdef OSC_SUPPORT
  if (_plugin) 
  {
        if (_oscif.oscGuiVisible())
                _oscif.oscShowGui(false);
        else
                _oscif.oscShowGui(true);
  }
  #endif
  _showNativeGuiPending = false;  
}

void PluginI::showNativeGui(bool flag)
{
  #ifdef OSC_SUPPORT
  if(_plugin) 
  {
    _oscif.oscShowGui(flag);
  }  
  #endif
  _showNativeGuiPending = false;  
}

//---------------------------------------------------------
//   nativeGuiVisible
//---------------------------------------------------------

bool PluginI::nativeGuiVisible()
{
  #ifdef OSC_SUPPORT
  return _oscif.oscGuiVisible();
  #endif    
  
  return false;
}

//---------------------------------------------------------
//   makeGui
//---------------------------------------------------------

void PluginI::makeGui()
      {
      _gui = new PluginGui(this);
      }

//---------------------------------------------------------
//   deleteGui
//---------------------------------------------------------
void PluginI::deleteGui()
{
  if(_gui)
  {
    delete _gui;
    _gui = 0;
  }  
}

//---------------------------------------------------------
//   enableAllControllers
//---------------------------------------------------------

void PluginI::enableAllControllers(bool v)
{
  for(int i = 0; i < controlPorts; ++i) 
    controls[i].enCtrl = v;
}

//---------------------------------------------------------
//   enable2AllControllers
//---------------------------------------------------------

void PluginI::enable2AllControllers(bool v)
{
  for(int i = 0; i < controlPorts; ++i) 
    controls[i].en2Ctrl = v;
}

//---------------------------------------------------------
//   apply
//---------------------------------------------------------

void PluginI::apply(int n)
{
      // Process control value changes.
      //if(automation && _track && _track->automationType() != AUTO_OFF && _id != -1)
      //{
      //  for(int i = 0; i < controlPorts; ++i)
      //  {
      //    if( controls[i].enCtrl && controls[i].en2Ctrl )
      //      controls[i].tmpVal = _track->pluginCtrlVal(genACnum(_id, i));
      //  }  
      //}      
      
      unsigned long ctls = controlPorts;
      for(unsigned long k = 0; k < ctls; ++k)
      {
        // First, update the temporary value if needed...
        
        #ifdef OSC_SUPPORT
        // Process OSC gui input control fifo events.
        // It is probably more important that these are processed so that they take precedence over all other
        //  events because OSC + DSSI/DSSI-VST are fussy about receiving feedback via these control ports, from GUI changes.
        
        OscControlFifo* cfifo = _oscif.oscFifo(k);
        //if(!cfifo)
        //  continue;
          
        // If there are 'events' in the fifo, get exactly one 'event' per control per process cycle...
        //if(!cfifo->isEmpty()) 
        if(cfifo && !cfifo->isEmpty()) 
        {
          OscControlValue v = cfifo->get();  
          
          #ifdef PLUGIN_DEBUGIN
          fprintf(stderr, "PluginI::apply OscControlFifo event input control number:%ld value:%f\n", k, v.value);
          #endif
          
          // Set the ladspa control port value.
          controls[k].tmpVal = v.value;
          
          // Need to update the automation value, otherwise it overwrites later with the last automation value.
          if(_track && _id != -1)
          {
            // Since we are now in the audio thread context, there's no need to send a message,
            //  just modify directly.
            //audio->msgSetPluginCtrlVal(this, genACnum(_id, k), controls[k].val);
            // p3.3.43
            //audio->msgSetPluginCtrlVal(_track, genACnum(_id, k), controls[k].val);
            _track->setPluginCtrlVal(genACnum(_id, k), v.value);
            
            // Record automation.
            // NO! Take care of this immediately in the OSC control handler, because we don't want 
            //  the silly delay associated with processing the fifo one-at-a-time here.
            
            //AutomationType at = _track->automationType();
            // TODO: Taken from our native gui control handlers. 
            // This may need modification or may cause problems - 
            //  we don't have the luxury of access to the dssi gui controls !
            //if(at == AUTO_WRITE || (audio->isPlaying() && at == AUTO_TOUCH))
            //  enableController(k, false);
            //_track->recordAutomation(id, v.value);
          }  
        }
        else
        #endif // OSC_SUPPORT
        {
          // Process automation control value.
          if(automation && _track && _track->automationType() != AUTO_OFF && _id != -1)
          {
            if(controls[k].enCtrl && controls[k].en2Ctrl )
              controls[k].tmpVal = _track->pluginCtrlVal(genACnum(_id, k));
          }      
        }
        
        // Now update the actual value from the temporary value...
        controls[k].val = controls[k].tmpVal;
      }  
      
      //for (int i = 0; i < controlPorts; ++i)
      //      controls[i].val = controls[i].tmpVal;
      
      for (int i = 0; i < instances; ++i)
      {
            // p3.3.41
            //fprintf(stderr, "PluginI::apply handle %d\n", i);
            _plugin->apply(handle[i], n);
      }      
      }

//---------------------------------------------------------
//   oscConfigure
//---------------------------------------------------------

#ifdef OSC_SUPPORT
int Plugin::oscConfigure(LADSPA_Handle handle, const char* key, const char* value)
      {
      #ifdef PLUGIN_DEBUGIN 
      printf("Plugin::oscConfigure effect plugin label:%s key:%s value:%s\n", plugin->Label, key, value);
      #endif
      
      #ifdef DSSI_SUPPORT
      if(!dssi_descr || !dssi_descr->configure)
            return 0;

      if (!strncmp(key, DSSI_RESERVED_CONFIGURE_PREFIX,
         strlen(DSSI_RESERVED_CONFIGURE_PREFIX))) {
            fprintf(stderr, "Plugin::oscConfigure OSC: UI for plugin '%s' attempted to use reserved configure key \"%s\", ignoring\n",
               plugin->Label, key);
               
            return 0;
            }

      char* message = dssi_descr->configure(handle, key, value);
      if (message) {
            printf("Plugin::oscConfigure on configure '%s' '%s', plugin '%s' returned error '%s'\n",
               //key, value, synti->name().toAscii().data(), message);
               key, value, plugin->Label, message);
            
            free(message);
            }

      // also call back on UIs for plugins other than the one
      // that requested this:
      // if (n != instance->number && instances[n].uiTarget) {
      //      lo_send(instances[n].uiTarget,
      //      instances[n].ui_osc_configure_path, "ss", key, value);
      //      }

      // configure invalidates bank and program information, so
      //  we should do this again now: 
      //queryPrograms();
      
      #endif // DSSI_SUPPORT
      
      return 0;
}
      
//---------------------------------------------------------
//   oscConfigure
//---------------------------------------------------------

int PluginI::oscConfigure(const char *key, const char *value)
      {
      if(!_plugin)
        return 0;

      // This is pretty much the simplest legal implementation of
      // configure in a DSSI host. 

      // The host has the option to remember the set of (key,value)
      // pairs associated with a particular instance, so that if it
      // wants to restore the "same" instance on another occasion it can
      // just call configure() on it for each of those pairs and so
      // restore state without any input from a GUI.  Any real-world GUI
      // host will probably want to do that.  This host doesn't have any
      // concept of restoring an instance from one run to the next, so
      // we don't bother remembering these at all. 

      //const char *key = (const char *)&argv[0]->s;
      //const char *value = (const char *)&argv[1]->s;

      #ifdef PLUGIN_DEBUGIN 
      printf("PluginI::oscConfigure effect plugin name:%s label:%s key:%s value:%s\n", _name.latin1(), _label.latin1(), key, value);
      #endif
      
      #ifdef DSSI_SUPPORT
      // FIXME: Don't think this is right, should probably do as example shows below.
      for(int i = 0; i < instances; ++i)
        _plugin->oscConfigure(handle[i], key, value);
      
      // also call back on UIs for plugins other than the one
      // that requested this:
      // if (n != instance->number && instances[n].uiTarget) {
      //      lo_send(instances[n].uiTarget,
      //      instances[n].ui_osc_configure_path, "ss", key, value);
      //      }

      // configure invalidates bank and program information, so
      //  we should do this again now: 
      //queryPrograms();
      #endif // DSSI_SUPPORT
      
      return 0;
}
      
//---------------------------------------------------------
//   oscUpdate
//---------------------------------------------------------

int PluginI::oscUpdate()
{
      #ifdef DSSI_SUPPORT
      // Send project directory.
      _oscif.oscSendConfigure(DSSI_PROJECT_DIRECTORY_KEY, museProject.latin1());  // song->projectPath()
      #endif
      
      /*
      // Send current string configuration parameters.
      StringParamMap& map = synti->stringParameters();
      int i = 0;
      for(ciStringParamMap r = map.begin(); r != map.end(); ++r) 
      {
        _oscIF.oscSendConfigure(r->first.c_str(), r->second.c_str());
        // Avoid overloading the GUI if there are lots and lots of params. 
        if((i+1) % 50 == 0)
          usleep(300000);
        ++i;      
      }  
      
      // Send current bank and program.
      unsigned long bank, prog;
      synti->currentProg(&prog, &bank, 0);
      _oscIF.oscSendProgram(prog, bank);
      
      // Send current control values.
      unsigned long ports = synth->_controlInPorts;
      for(unsigned long i = 0; i < ports; ++i) 
      {
        unsigned long k = synth->pIdx(i);
        _oscIF.oscSendControl(k, controls[i]);
        // Avoid overloading the GUI if there are lots and lots of ports. 
        if((i+1) % 50 == 0)
          usleep(300000);
      }
      
      */
      
      return 0;
}

//---------------------------------------------------------
//   oscControl
//---------------------------------------------------------

int PluginI::oscControl(unsigned long port, float value)
{
  //int port = argv[0]->i;
  //LADSPA_Data value = argv[1]->f;

  #ifdef PLUGIN_DEBUGIN  
  printf("PluginI::oscControl received oscControl port:%ld val:%f\n", port, value);
  #endif
  
  //int controlPorts = synth->_controller;
  
  //if(port >= controlPorts)
  //if(port < 0 || port >= _plugin->rpIdx.size())
  //{
    //fprintf(stderr, "DssiSynthIF::oscControl: port number:%d is out of range of number of ports:%d\n", port, controlPorts);
  //  fprintf(stderr, "PluginI::oscControl: port number:%d is out of range of index list size:%d\n", port, _plugin->rpIdx.size());
  //  return 0;
  //}
  
  // Convert from DSSI port number to control input port index.
  //unsigned long cport = _plugin->rpIdx[port];
  unsigned long cport = _plugin->port2InCtrl(port);
    
  if((int)cport == -1)
  {
    fprintf(stderr, "PluginI::oscControl: port number:%ld is not a control input\n", port);
    return 0;
  }
  
  // (From DSSI module).
  // p3.3.39 Set the DSSI control input port's value.
  // Observations: With a native DSSI synth like LessTrivialSynth, the native GUI's controls do not change the sound at all
  //  ie. they don't update the DSSI control port values themselves. 
  // Hence in response to the call to this oscControl, sent by the native GUI, it is required to that here.
///  controls[cport].val = value;
  // DSSI-VST synths however, unlike DSSI synths, DO change their OWN sound in response to their gui controls.
  // AND this function is called ! 
  // Despite the descrepency we are STILL required to update the DSSI control port values here 
  //  because dssi-vst is WAITING FOR A RESPONSE! (A CHANGE in the control port value). 
  // It will output something like "...4 events expected..." and count that number down as 4 actual control port value CHANGES
  //  are done here in response. Normally it says "...0 events expected..." when MusE is the one doing the DSSI control changes.
  // TODO: May need FIFOs on each control(!) so that the control changes get sent one per process cycle! 
  // Observed countdown not actually going to zero upon string of changes.
  // Try this ...
  OscControlFifo* cfifo = _oscif.oscFifo(cport); 
  if(cfifo)
  {
    OscControlValue cv;
    //cv.idx = cport;
    cv.value = value;
    if(cfifo->put(cv))
    {
      fprintf(stderr, "PluginI::oscControl: fifo overflow: in control number:%ld\n", cport);
    }
  }
   
  // Record automation:
  // Take care of this immediately, because we don't want the silly delay associated with 
  //  processing the fifo one-at-a-time in the apply().
  // NOTE: Ahh crap! We don't receive control events until the user RELEASES a control !
  // So the events all arrive at once when the user releases a control.
  // That makes this pretty useless... But what the heck...
  if(_track && _id != -1)
  {
    int id = genACnum(_id, cport);
    AutomationType at = _track->automationType();
  
    // TODO: Taken from our native gui control handlers. 
    // This may need modification or may cause problems - 
    //  we don't have the luxury of access to the dssi gui controls !
    if(at == AUTO_WRITE || (audio->isPlaying() && at == AUTO_TOUCH))
      enableController(cport, false);
      
    _track->recordAutomation(id, value);
  } 
   
  /*
  const DSSI_Descriptor* dssi = synth->dssi;
  const LADSPA_Descriptor* ld = dssi->LADSPA_Plugin;
  
  ciMidiCtl2LadspaPort ip = synth->port2MidiCtlMap.find(cport);
  if(ip != synth->port2MidiCtlMap.end())
  {
    // TODO: TODO: Update midi MusE's midi controller knobs, sliders, boxes etc with a call to the midi port's setHwCtrlState() etc.
    // But first we need a ladspa2MidiValue() function!  ... 
    //
    //
    //float val = ladspa2MidiValue(ld, i, ?, ?); 
  
  }
  */

#if 0
      int port = argv[0]->i;
      LADSPA_Data value = argv[1]->f;

      if (port < 0 || port > instance->plugin->descriptor->LADSPA_Plugin->PortCount) {
            fprintf(stderr, "MusE: OSC: %s port number (%d) is out of range\n",
               instance->friendly_name, port);
            return 0;
            }
      if (instance->pluginPortControlInNumbers[port] == -1) {
            fprintf(stderr, "MusE: OSC: %s port %d is not a control in\n",
               instance->friendly_name, port);
            return 0;
            }
      pluginControlIns[instance->pluginPortControlInNumbers[port]] = value;
      if (verbose) {
            printf("MusE: OSC: %s port %d = %f\n",
               instance->friendly_name, port, value);
            }
#endif
      return 0;
      }

#endif // OSC_SUPPORT


//---------------------------------------------------------
//   PluginDialog
//    select Plugin dialog
//---------------------------------------------------------

PluginDialog::PluginDialog(QWidget* parent, const char* name, bool modal)
  : QDialog(parent, name, modal)
      {
      setCaption(tr("MusE: select plugin"));
      QVBoxLayout* layout = new QVBoxLayout(this);

      pList  = new Q3ListView(this);
      pList->setAllColumnsShowFocus(true);
      pList->addColumn(tr("Lib"),   110);
      pList->addColumn(tr("Label"), 110);
      pList->addColumn(tr("Name"),  200);
      pList->addColumn(tr("AI"),    30);
      pList->addColumn(tr("AO"),    30);
      pList->addColumn(tr("CI"),    30);
      pList->addColumn(tr("CO"),    30);
      pList->addColumn(tr("IP"),    30);
      pList->addColumn(tr("id"),    40);
      pList->addColumn(tr("Maker"), 110);
      pList->addColumn(tr("Copyright"), 110);
      pList->setColumnWidthMode(1, Q3ListView::Maximum);

      layout->addWidget(pList);

      //---------------------------------------------------
      //  Ok/Cancel Buttons
      //---------------------------------------------------

      QBoxLayout* w5 = new QHBoxLayout;
      layout->addLayout(w5);

      QPushButton* okB     = new QPushButton(tr("Ok"), this);
      okB->setDefault(true);
      QPushButton* cancelB = new QPushButton(tr("Cancel"), this);
      okB->setFixedWidth(80);
      cancelB->setFixedWidth(80);
      w5->addWidget(okB);
      w5->addSpacing(12);
      w5->addWidget(cancelB);

      // ORCAN - CHECK
      //QButtonGroup* plugSel = new QButtonGroup(4, Qt::Horizontal, this, "Show plugs:");
      QGroupBox* plugSel = new QGroupBox(this);
      plugSel->setTitle("Show plugs:");
      QRadioButton* onlySM = new QRadioButton(plugSel, "Mono and Stereo");
      onlySM->setText( "Mono and Stereo");
      QRadioButton* onlyS = new QRadioButton(plugSel, "Stereo");
      onlyS->setText( "Stereo");
      QRadioButton* onlyM = new QRadioButton(plugSel, "Mono");
      onlyM->setText( "Mono");
      QRadioButton* allPlug = new QRadioButton(plugSel, "Show all");
      allPlug->setText( "Show All");

      // ORCAN - FIXME
      //plugSel->setRadioButtonExclusive(true);
      //plugSel->setButton(selectedPlugType);

      QToolTip::add(plugSel, tr("Select which types of plugins should be visible in the list.<br>"
                             "Note that using mono plugins on stereo tracks is not a problem, two will be used in parallel.<br>"
                             "Also beware that the 'all' alternative includes plugins that probably are not usable by MusE."));

      onlySM->setCaption(tr("Stereo and Mono"));
      onlyS->setCaption(tr("Stereo"));
      onlyM->setCaption(tr("Mono"));
      allPlug->setCaption(tr("All"));

      w5->addSpacing(12);
      w5->addWidget(plugSel);
      w5->addSpacing(12);
      
      QLabel *sortLabel = new QLabel( this, "Search in 'Label' and 'Name':" );
      sortLabel->setText( "Search in 'Label' and 'Name':" );
      w5->addWidget(sortLabel);
      w5->addSpacing(2);
      
      sortBox = new QComboBox( true, this, "sort" );
      if (!sortItems.empty())
          sortBox->insertStringList(sortItems);
      
      sortBox->setMinimumSize( 100, 10);
      w5->addWidget(sortBox);
      w5->addStretch(-1);
      
      if (sortBox->currentText().length() > 0)
          fillPlugs(sortBox->currentText());
      else
          fillPlugs(selectedPlugType);
      

      connect(pList,   SIGNAL(doubleClicked(Q3ListViewItem*)), SLOT(accept()));
      connect(cancelB, SIGNAL(clicked()), SLOT(reject()));
      connect(okB,     SIGNAL(clicked()), SLOT(accept()));
      connect(plugSel, SIGNAL(clicked(int)), SLOT(fillPlugs(int)));
      connect(sortBox, SIGNAL(textChanged(const QString&)),SLOT(fillPlugs(const QString&)));
      sortBox->setFocus();
      }

//---------------------------------------------------------
//   value
//---------------------------------------------------------

Plugin* PluginDialog::value()
      {
      Q3ListViewItem* item = pList->selectedItem();
      if (item)
            return plugins.find(item->text(0), item->text(1));
      return 0;
      }

//---------------------------------------------------------
//   accept
//---------------------------------------------------------

void PluginDialog::accept()
      {
      if (!sortBox->currentText().isEmpty()) {
          if (sortItems.find(sortBox->currentText()) == sortItems.end())
              sortItems.push_front(sortBox->currentText());
      }
      QDialog::accept();
      }

//---------------------------------------------------------
//    fillPlugs
//---------------------------------------------------------

void PluginDialog::fillPlugs(int nbr)
{
  pList->clear();
  for(iPlugin i = plugins.begin(); i != plugins.end(); ++i) 
  {
    /*
    int ai = 0;
    int ao = 0;
    int ci = 0;
    int co = 0;
    for(unsigned long k = 0; k < i->ports(); ++k) 
    {
      LADSPA_PortDescriptor pd = i->portd(k);
      if(pd & LADSPA_PORT_CONTROL) 
      {
        if(pd & LADSPA_PORT_INPUT)
          ++ci;
        else
        if(pd & LADSPA_PORT_OUTPUT)
          ++co;
      }
      else 
      if(pd & LADSPA_PORT_AUDIO) 
      {
        if(pd & LADSPA_PORT_INPUT)
          ++ai;
        else
        if(pd & LADSPA_PORT_OUTPUT)
          ++ao;
      }
    }
    */
    int ai = i->inports();
    int ao = i->outports();
    int ci = i->controlInPorts();
    int co = i->controlOutPorts();
    
    bool addFlag = false;
    switch(nbr)
    {
        case 0: // stereo & mono
          if ((ai == 1 || ai == 2) && (ao == 1 || ao ==2))
            {
            addFlag = true;
            }
          break;
        case 1: // stereo
          if ((ai == 1 || ai == 2) &&  ao ==2)
            {
            addFlag = true;
            }
          break;
        case 2: // mono
          if (ai == 1  && ao == 1)
            {
            addFlag = true;
            }
          break;
        case 3: // all
            addFlag = true;
          break;
    }
    if(addFlag)
    {
      Q3ListViewItem* item = new Q3ListViewItem(pList,
        i->lib(),
        i->label(),
        i->name(),
        QString().setNum(ai),
        QString().setNum(ao),
        QString().setNum(ci),
        QString().setNum(co),
        QString().setNum(i->inPlaceCapable())
      );
      item->setText(8, QString().setNum(i->id()));
      item->setText(9, i->maker());
      item->setText(10, i->copyright());
    }
  }
  selectedPlugType = nbr;
}

void PluginDialog::fillPlugs(const QString &sortValue)
{
  pList->clear();
  for(iPlugin i = plugins.begin(); i != plugins.end(); ++i) 
  {
    /*
    int ai = 0;
    int ao = 0;
    int ci = 0;
    int co = 0;
    for(unsigned long k = 0; k < i->ports(); ++k) 
    {
      LADSPA_PortDescriptor pd = i->portd(k);
      if(pd & LADSPA_PORT_CONTROL) 
      {
        if(pd & LADSPA_PORT_INPUT)
          ++ci;
        else
        if(pd & LADSPA_PORT_OUTPUT)
          ++co;
      }
      else 
      if(pd & LADSPA_PORT_AUDIO) 
      {
        if(pd & LADSPA_PORT_INPUT)
          ++ai;
        else
        if(pd & LADSPA_PORT_OUTPUT)
          ++ao;
      }
    }
    */
    int ai = i->inports();
    int ao = i->outports();
    int ci = i->controlInPorts();
    int co = i->controlOutPorts();
    
    bool addFlag = false;
    
    if(i->label().lower().contains(sortValue.lower()))
      addFlag = true;
    else
    if(i->name().lower().contains(sortValue.lower()))
      addFlag = true;
    if(addFlag)
    {
      Q3ListViewItem* item = new Q3ListViewItem(pList,
        i->lib(),
        i->label(),
        i->name(),
        QString().setNum(ai),
        QString().setNum(ao),
        QString().setNum(ci),
        QString().setNum(co),
        QString().setNum(i->inPlaceCapable())
      );
      item->setText(8, QString().setNum(i->id()));
      item->setText(9, i->maker());
      item->setText(10, i->copyright());
    }
  }
}

//---------------------------------------------------------
//   getPlugin
//---------------------------------------------------------

Plugin* PluginDialog::getPlugin(QWidget* parent)
      {
      PluginDialog* dialog = new PluginDialog(parent);
      if (dialog->exec())
            return dialog->value();
      return 0;
      }

const char* presetOpenText = "<img source=\"fileopen\"> "
      "Click this button to load a saved <em>preset</em>.";
const char* presetSaveText = "Click this button to save curent parameter "
      "settings as a <em>preset</em>.  You will be prompted for a file name.";
const char* presetBypassText = "Click this button to bypass effect unit";

//---------------------------------------------------------
//   PluginGui
//---------------------------------------------------------

//PluginGui::PluginGui(PluginI* p)
// p3.3.43
PluginGui::PluginGui(PluginIBase* p)
   : Q3MainWindow(0)
      {
      gw     = 0;
      params = 0;
      plugin = p;
      setCaption(plugin->name());

      QToolBar* tools = new QToolBar(tr("File Buttons"), this);
      QToolButton* fileOpen = new QToolButton(
         QIcon(*openIconS), // ddskrjo
         tr("Load Preset"),
     QString::null, this, SLOT(load()),
     tools, "load preset" );

      QToolButton* fileSave = new QToolButton(
         QIcon(*saveIconS), // ddskrjo
         tr("Save Preset"),
     QString::null,
     this, SLOT(save()),
     tools, "save preset");

      Q3WhatsThis::whatsThisButton(tools);

      onOff = new QToolButton(tools, "bypass");
      onOff->setIconSet(*exitIconS);
      onOff->setToggleButton(true);
      onOff->setOn(plugin->on());
      QToolTip::add(onOff, tr("bypass plugin"));
      connect(onOff, SIGNAL(toggled(bool)), SLOT(bypassToggled(bool)));

      Q3WhatsThis::add(fileOpen, tr(presetOpenText));
      Q3WhatsThis::add(onOff, tr(presetBypassText));
      Q3MimeSourceFactory::defaultFactory()->setPixmap(QString("fileopen"), *openIcon );
      Q3WhatsThis::add(fileSave, tr(presetSaveText));

      QString id;
      //id.setNum(plugin->plugin()->id());
      id.setNum(plugin->pluginID());
      QString name(museGlobalShare + QString("/plugins/") + id + QString(".ui"));
      QFile uifile(name);
      if (uifile.exists()) {
            //
            // construct GUI from *.ui file
            //
#if 0 // ddskrjo
            mw = QWidgetFactory::create(uifile.name(), 0, this);
            setCentralWidget(mw);

            const QObjectList* l = mw->children();
            QObject *obj;

            nobj = 0;
            QObjectListIt it(*l);
            while ((obj = it.current()) != 0) {
                  ++it;
                  const char* name = obj->name();
                  if (*name !='P')
                        continue;
                  int parameter = -1;
                  sscanf(name, "P%d", &parameter);
                  if (parameter == -1)
                        continue;
                  ++nobj;
                  }
            it.toFirst();
            gw   = new GuiWidgets[nobj];
            nobj = 0;
            QSignalMapper* mapper = new QSignalMapper(this, "pluginGuiMapper");
            connect(mapper, SIGNAL(mapped(int)), SLOT(guiParamChanged(int)));
            
            QSignalMapper* mapperPressed = new QSignalMapper(this, "pluginGuiMapperPressed");
            QSignalMapper* mapperReleased = new QSignalMapper(this, "pluginGuiMapperReleased");
            connect(mapperPressed, SIGNAL(mapped(int)), SLOT(guiParamPressed(int)));
            connect(mapperReleased, SIGNAL(mapped(int)), SLOT(guiParamReleased(int)));
            
            while ((obj = it.current()) != 0) {
                  ++it;
                  const char* name = obj->name();
                  if (*name !='P')
                        continue;
                  int parameter = -1;
                  sscanf(name, "P%d", &parameter);
                  if (parameter == -1)
                        continue;

                  mapper->setMapping(obj, nobj);
                  mapperPressed->setMapping(obj, nobj);
                  mapperReleased->setMapping(obj, nobj);
                  
                  gw[nobj].widget = (QWidget*)obj;
                  gw[nobj].param  = parameter;
                  gw[nobj].type   = -1;

                  if (strcmp(obj->className(), "Slider") == 0) {
                        gw[nobj].type = GuiWidgets::SLIDER;
                        ((Slider*)obj)->setId(nobj);
                        ((Slider*)obj)->setCursorHoming(true);
                        for(int i = 0; i < nobj; i++)
                        {
                          if(gw[i].type == GuiWidgets::DOUBLE_LABEL && gw[i].param == parameter)
                            ((DoubleLabel*)gw[i].widget)->setSlider((Slider*)obj);
                        }
                        connect(obj, SIGNAL(sliderMoved(double,int)), mapper, SLOT(map()));
                        connect(obj, SIGNAL(sliderPressed(int)), SLOT(guiSliderPressed(int)));
                        connect(obj, SIGNAL(sliderReleased(int)), SLOT(guiSliderReleased(int)));
                        connect(obj, SIGNAL(sliderRightClicked(const QPoint &, int)), SLOT(guiSliderRightClicked(const QPoint &, int)));
                        }
                  else if (strcmp(obj->className(), "DoubleLabel") == 0) {
                        gw[nobj].type = GuiWidgets::DOUBLE_LABEL;
                        ((DoubleLabel*)obj)->setId(nobj);
                        for(int i = 0; i < nobj; i++)
                        {
                          if(gw[i].type == GuiWidgets::SLIDER && gw[i].param == parameter)
                          {
                            ((DoubleLabel*)obj)->setSlider((Slider*)gw[i].widget);
                            break;  
                          }  
                        }
                        connect(obj, SIGNAL(valueChanged(double,int)), mapper, SLOT(map()));
                        }
                  else if (strcmp(obj->className(), "QCheckBox") == 0) {
                        gw[nobj].type = GuiWidgets::QCHECKBOX;
                        connect(obj, SIGNAL(toggled(bool)), mapper, SLOT(map()));
                        connect(obj, SIGNAL(pressed()), mapperPressed, SLOT(map()));
                        connect(obj, SIGNAL(released()), mapperReleased, SLOT(map()));
                        }
                  else if (strcmp(obj->className(), "QComboBox") == 0) {
                        gw[nobj].type = GuiWidgets::QCOMBOBOX;
                        connect(obj, SIGNAL(activated(int)), mapper, SLOT(map()));
                        }
                  else {
                        printf("unknown widget class %s\n", obj->className());
                        continue;
                        }
                  ++nobj;
                  }
              updateValues(); // otherwise the GUI won't have valid data
#endif
            }
      else {
            //mw = new QWidget(this);
            //setCentralWidget(mw);
            // p3.4.43
            view = new Q3ScrollView(this);
            setCentralWidget(view);
            mw = new QWidget(view);
            view->setResizePolicy(Q3ScrollView::AutoOneFit);
            //view->setVScrollBarMode(QScrollView::AlwaysOff);
            view->addChild(mw);
            
            QGridLayout* grid = new QGridLayout(mw);
            grid->setSpacing(2);

            int n  = plugin->parameters();
            params = new GuiParam[n];

            // Changed p3.3.43
            //resize(280, n*20+30);
            //int nh = n*20+40;
            //if(nh > 760)
            //  nh = 760;
            //resize(280, nh);

            //int style       = Slider::BgTrough | Slider::BgSlot;
            QFontMetrics fm = fontMetrics();
            int h           = fm.height() + 4;

            for (int i = 0; i < n; ++i) {
                  QLabel* label = 0;
                  LADSPA_PortRangeHint range = plugin->range(i);
                  double lower = 0.0;     // default values
                  double upper = 1.0;
                  double dlower = lower;
                  double dupper = upper;
                  double val   = plugin->param(i);
                  double dval  = val;
                  params[i].hint = range.HintDescriptor;

                  if (LADSPA_IS_HINT_BOUNDED_BELOW(range.HintDescriptor)) {
                        dlower = lower = range.LowerBound;
                        }
                  if (LADSPA_IS_HINT_BOUNDED_ABOVE(range.HintDescriptor)) {
                        dupper = upper = range.UpperBound;
                        }
                  if (LADSPA_IS_HINT_SAMPLE_RATE(range.HintDescriptor)) {
                        lower *= sampleRate;
                        upper *= sampleRate;
                        dlower = lower;
                        dupper = upper;
                        }
                  if (LADSPA_IS_HINT_LOGARITHMIC(range.HintDescriptor)) {
                        if (lower == 0.0)
                              lower = 0.001;
                        dlower = fast_log10(lower)*20.0;
                        dupper = fast_log10(upper)*20.0;
                        dval  = fast_log10(val) * 20.0;
                        }
                  if (LADSPA_IS_HINT_TOGGLED(range.HintDescriptor)) {
                        params[i].type = GuiParam::GUI_SWITCH;
                        CheckBox* cb = new CheckBox(mw, i, "param");
                        cb->setId(i);
                        cb->setText(QString(plugin->paramName(i)));
                        cb->setChecked(plugin->param(i) != 0.0);
                        cb->setFixedHeight(h);
                        params[i].actuator = cb;
                        }
                  else {
                        label           = new QLabel(QString(plugin->paramName(i)), mw);
                        params[i].type  = GuiParam::GUI_SLIDER;
                        params[i].label = new DoubleLabel(val, lower, upper, mw);
                        params[i].label->setFrame(true);
                        params[i].label->setPrecision(2);
                        params[i].label->setId(i);

                        //params[i].label->setMargin(2);
                        //params[i].label->setFixedHeight(h);

                        Slider* s = new Slider(mw, "param", Qt::Horizontal,
                           Slider::None); //, style);
                           
                        s->setCursorHoming(true);
                        s->setId(i);
                        //s->setFixedHeight(h);
                        s->setRange(dlower, dupper);
                        if(LADSPA_IS_HINT_INTEGER(range.HintDescriptor))
                          s->setStep(1.0);
                        s->setValue(dval);
                        params[i].actuator = s;
                        params[i].label->setSlider((Slider*)params[i].actuator);
                        }
                  //params[i].actuator->setSizePolicy(QSizePolicy(QSizePolicy::Minimum, QSizePolicy::Maximum));
                  params[i].actuator->setSizePolicy(QSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed));
                  if (params[i].type == GuiParam::GUI_SLIDER) {
                        //label->setFixedHeight(20);
                        //label->setSizePolicy(QSizePolicy(QSizePolicy::Minimum, QSizePolicy::Maximum));
                        label->setSizePolicy(QSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed));
                        //params[i].label->setSizePolicy(QSizePolicy(QSizePolicy::Minimum, QSizePolicy::Maximum));
                        params[i].label->setSizePolicy(QSizePolicy(QSizePolicy::Minimum, QSizePolicy::Fixed));
                        grid->addWidget(label, i, 0);
                        grid->addWidget(params[i].label,    i, 1);
                        grid->addWidget(params[i].actuator, i, 2);
                        }
                  else if (params[i].type == GuiParam::GUI_SWITCH) {
                        grid->addMultiCellWidget(params[i].actuator, i, i, 0, 2);
                        }
                  if (params[i].type == GuiParam::GUI_SLIDER) {
                        connect(params[i].actuator, SIGNAL(sliderMoved(double,int)), SLOT(sliderChanged(double,int)));
                        connect(params[i].label,    SIGNAL(valueChanged(double,int)), SLOT(labelChanged(double,int)));
                        connect(params[i].actuator, SIGNAL(sliderPressed(int)), SLOT(ctrlPressed(int)));
                        connect(params[i].actuator, SIGNAL(sliderReleased(int)), SLOT(ctrlReleased(int)));
                        connect(params[i].actuator, SIGNAL(sliderRightClicked(const QPoint &, int)), SLOT(ctrlRightClicked(const QPoint &, int)));
                        }
                  else if (params[i].type == GuiParam::GUI_SWITCH){
                        connect(params[i].actuator, SIGNAL(checkboxPressed(int)), SLOT(ctrlPressed(int)));
                        connect(params[i].actuator, SIGNAL(checkboxReleased(int)), SLOT(ctrlReleased(int)));
                        connect(params[i].actuator, SIGNAL(checkboxRightClicked(const QPoint &, int)), SLOT(ctrlRightClicked(const QPoint &, int)));
                        }
                  }
            // p3.3.43
            resize(280, height());
            
            grid->setColStretch(2, 10);
            }
      connect(heartBeatTimer, SIGNAL(timeout()), SLOT(heartBeat()));
      }

//---------------------------------------------------------
//   PluginGui
//---------------------------------------------------------

PluginGui::~PluginGui()
      {
      if (gw)
            delete[] gw;
      if (params)
            delete[] params;
      }

//---------------------------------------------------------
//   heartBeat
//---------------------------------------------------------

void PluginGui::heartBeat()
{
  updateControls();
}

//---------------------------------------------------------
//   ctrlPressed
//---------------------------------------------------------

void PluginGui::ctrlPressed(int param)
{
      AutomationType at = AUTO_OFF;
      AudioTrack* track = plugin->track();
      if(track)
        at = track->automationType();
            
      if(at != AUTO_OFF)
        plugin->enableController(param, false);
      
      int id = plugin->id();
      
      if(id == -1)
        return;
        
      id = genACnum(id, param);
      
      if(params[param].type == GuiParam::GUI_SLIDER)
      {
        double val = ((Slider*)params[param].actuator)->value();
        if (LADSPA_IS_HINT_LOGARITHMIC(params[param].hint))
              val = pow(10.0, val/20.0);
        else if (LADSPA_IS_HINT_INTEGER(params[param].hint))
              val = rint(val);
        plugin->setParam(param, val);
        ((DoubleLabel*)params[param].label)->setValue(val);
        
        // p3.3.43
        //audio->msgSetPluginCtrlVal(((PluginI*)plugin), id, val);
        
        if(track)
        {
          // p3.3.43
          audio->msgSetPluginCtrlVal(track, id, val);
          
          track->startAutoRecord(id, val);
        }  
      }       
      else if(params[param].type == GuiParam::GUI_SWITCH)
      {
        double val = (double)((CheckBox*)params[param].actuator)->isChecked();
        plugin->setParam(param, val);
        
        // p3.3.43
        //audio->msgSetPluginCtrlVal(((PluginI*)plugin), id, val);
        
        if(track)
        {
          // p3.3.43
          audio->msgSetPluginCtrlVal(track, id, val);
          
          track->startAutoRecord(id, val);
        }  
      }       
}

//---------------------------------------------------------
//   ctrlReleased
//---------------------------------------------------------

void PluginGui::ctrlReleased(int param)
{
      AutomationType at = AUTO_OFF;
      AudioTrack* track = plugin->track();
      if(track)
        at = track->automationType();
        
      // Special for switch - don't enable controller until transport stopped.
      if(at != AUTO_WRITE && ((params[param].type != GuiParam::GUI_SWITCH
          || !audio->isPlaying()
          || at != AUTO_TOUCH) || (!audio->isPlaying() && at == AUTO_TOUCH)) )
        plugin->enableController(param, true);
      
      int id = plugin->id();
      if(!track || id == -1)
        return;
      id = genACnum(id, param);
        
      if(params[param].type == GuiParam::GUI_SLIDER)
      {
        double val = ((Slider*)params[param].actuator)->value();
        if (LADSPA_IS_HINT_LOGARITHMIC(params[param].hint))
              val = pow(10.0, val/20.0);
        else if (LADSPA_IS_HINT_INTEGER(params[param].hint))
              val = rint(val);
        track->stopAutoRecord(id, val);
      }       
      //else if(params[param].type == GuiParam::GUI_SWITCH)
      //{
        //double val = (double)((CheckBox*)params[param].actuator)->isChecked();
        // No concept of 'untouching' a checkbox. Remain 'touched' until stop.
        //plugin->track()->stopAutoRecord(genACnum(plugin->id(), param), val);
      //}       
}

//---------------------------------------------------------
//   ctrlRightClicked
//---------------------------------------------------------

void PluginGui::ctrlRightClicked(const QPoint &p, int param)
{
  int id = plugin->id();
  if(id != -1)
    //song->execAutomationCtlPopup((AudioTrack*)plugin->track(), p, genACnum(id, param));
    song->execAutomationCtlPopup(plugin->track(), p, genACnum(id, param));
}

//---------------------------------------------------------
//   sliderChanged
//---------------------------------------------------------

void PluginGui::sliderChanged(double val, int param)
{
      AutomationType at = AUTO_OFF;
      AudioTrack* track = plugin->track();
      if(track)
        at = track->automationType();
      
      if(at == AUTO_WRITE || (audio->isPlaying() && at == AUTO_TOUCH))
        plugin->enableController(param, false);
      
      if (LADSPA_IS_HINT_LOGARITHMIC(params[param].hint))
            val = pow(10.0, val/20.0);
      else if (LADSPA_IS_HINT_INTEGER(params[param].hint))
            val = rint(val);
      if (plugin->param(param) != val) {
            plugin->setParam(param, val);
            ((DoubleLabel*)params[param].label)->setValue(val);
            }
            
      int id = plugin->id();
      if(id == -1)
        return;
      id = genACnum(id, param);
          
      // p3.3.43
      //audio->msgSetPluginCtrlVal(((PluginI*)plugin), id, val);
          
      if(track)
      {
        // p3.3.43
        audio->msgSetPluginCtrlVal(track, id, val);
        
        track->recordAutomation(id, val);
      }  
}

//---------------------------------------------------------
//   labelChanged
//---------------------------------------------------------

void PluginGui::labelChanged(double val, int param)
{
      AutomationType at = AUTO_OFF;
      AudioTrack* track = plugin->track();
      if(track)
        at = track->automationType();
      
      if(at == AUTO_WRITE || (audio->isPlaying() && at == AUTO_TOUCH))
        plugin->enableController(param, false);
      
      double dval = val;
      if (LADSPA_IS_HINT_LOGARITHMIC(params[param].hint))
            dval = fast_log10(val) * 20.0;
      else if (LADSPA_IS_HINT_INTEGER(params[param].hint))
            dval = rint(val);
      if (plugin->param(param) != val) {
            plugin->setParam(param, val);
            ((Slider*)params[param].actuator)->setValue(dval);
            }
      
      int id = plugin->id();
      if(id == -1)
        return;
      
      id = genACnum(id, param);
      
      // p3.3.43
      //audio->msgSetPluginCtrlVal(((PluginI*)plugin), id, val);
      
      if(track)
      {
        // p3.3.43
        audio->msgSetPluginCtrlVal(track, id, val);
        
        track->startAutoRecord(id, val);
      }  
}

//---------------------------------------------------------
//   load
//---------------------------------------------------------

void PluginGui::load()
      {
      QString s("presets/plugins/");
      //s += plugin->plugin()->label();
      s += plugin->pluginLabel();
      s += "/";

      QString fn = getOpenFileName(s, preset_file_pattern,
         this, tr("MusE: load preset"), 0);
      if (fn.isEmpty())
            return;
      bool popenFlag;
      FILE* f = fileOpen(this, fn, QString(".pre"), "r", popenFlag, true);
      if (f == 0)
            return;

      Xml xml(f);
      int mode = 0;
      for (;;) {
            Xml::Token token = xml.parse();
            QString tag = xml.s1();
            switch (token) {
                  case Xml::Error:
                  case Xml::End:
                        return;
                  case Xml::TagStart:
                        if (mode == 0 && tag == "muse")
                              mode = 1;
                        else if (mode == 1 && tag == "plugin") {
                              
                              if(plugin->readConfiguration(xml, true))
                              {
                                QMessageBox::critical(this, QString("MusE"),
                                  tr("Error reading preset. Might not be right type for this plugin"));
                                goto ende;
                              }
                                
                              mode = 0;
                              }
                        else
                              xml.unknown("PluginGui");
                        break;
                  case Xml::Attribut:
                        break;
                  case Xml::TagEnd:
                        if (!mode && tag == "muse")
                        {
                              plugin->updateControllers();
                              goto ende;
                        }     
                  default:
                        break;
                  }
            }
ende:
      if (popenFlag)
            pclose(f);
      else
            fclose(f);
      }

//---------------------------------------------------------
//   save
//---------------------------------------------------------

void PluginGui::save()
      {
      QString s("presets/plugins/");
      //s += plugin->plugin()->label();
      s += plugin->pluginLabel();
      s += "/";

      //QString fn = getSaveFileName(s, preset_file_pattern, this,
      QString fn = getSaveFileName(s, preset_file_save_pattern, this,
        tr("MusE: save preset"));
      if (fn.isEmpty())
            return;
      bool popenFlag;
      FILE* f = fileOpen(this, fn, QString(".pre"), "w", popenFlag, false, true);
      if (f == 0)
            return;
      Xml xml(f);
      xml.header();
      xml.tag(0, "muse version=\"1.0\"");
      plugin->writeConfiguration(1, xml);
      xml.tag(1, "/muse");

      if (popenFlag)
            pclose(f);
      else
            fclose(f);
      }

//---------------------------------------------------------
//   bypassToggled
//---------------------------------------------------------

void PluginGui::bypassToggled(bool val)
      {
      plugin->setOn(val);
      song->update(SC_ROUTE);
      }

//---------------------------------------------------------
//   songChanged
//---------------------------------------------------------

void PluginGui::setOn(bool val)
      {
      onOff->blockSignals(true);
      onOff->setOn(val);
      onOff->blockSignals(false);
      }

//---------------------------------------------------------
//   updateValues
//---------------------------------------------------------

void PluginGui::updateValues()
      {
      if (params) {
            for (int i = 0; i < plugin->parameters(); ++i) {
                  GuiParam* gp = &params[i];
                  if (gp->type == GuiParam::GUI_SLIDER) {
                        double lv = plugin->param(i);
                        double sv = lv;
                        if (LADSPA_IS_HINT_LOGARITHMIC(params[i].hint))
                              sv = fast_log10(lv) * 20.0;
                        else if (LADSPA_IS_HINT_INTEGER(params[i].hint))
                        {
                              sv = rint(lv);
                              lv = sv;
                        }      
                        gp->label->setValue(lv);
                        ((Slider*)(gp->actuator))->setValue(sv);
                        }
                  else if (gp->type == GuiParam::GUI_SWITCH) {
                        ((CheckBox*)(gp->actuator))->setChecked(int(plugin->param(i)));
                        }
                  }
            }
      else if (gw) {
            for (int i = 0; i < nobj; ++i) {
                  QWidget* widget = gw[i].widget;
                  int type = gw[i].type;
                  int param = gw[i].param;
                  double val = plugin->param(param);
                  switch(type) {
                        case GuiWidgets::SLIDER:
                              ((Slider*)widget)->setValue(val);
                              break;
                        case GuiWidgets::DOUBLE_LABEL:
                              ((DoubleLabel*)widget)->setValue(val);
                              break;
                        case GuiWidgets::QCHECKBOX:
                              ((QCheckBox*)widget)->setChecked(int(val));
                              break;
                        case GuiWidgets::QCOMBOBOX:
                              ((QComboBox*)widget)->setCurrentItem(int(val));
                              break;
                        }
                  }
            }
      }

//---------------------------------------------------------
//   updateControls
//---------------------------------------------------------

void PluginGui::updateControls()
      {
      if(!automation || !plugin->track() || plugin->id() == -1)
        return;
      AutomationType at = plugin->track()->automationType();
      if(at == AUTO_OFF)
        return;
      if (params) {
            for (int i = 0; i < plugin->parameters(); ++i) {
                  GuiParam* gp = &params[i];
                  if (gp->type == GuiParam::GUI_SLIDER) {
                        if( plugin->controllerEnabled(i) && plugin->controllerEnabled2(i) )
                          {
                            double lv = plugin->track()->pluginCtrlVal(genACnum(plugin->id(), i));
                            double sv = lv;
                            if (LADSPA_IS_HINT_LOGARITHMIC(params[i].hint))
                                  sv = fast_log10(lv) * 20.0;
                            else 
                            if (LADSPA_IS_HINT_INTEGER(params[i].hint))
                            {
                                  sv = rint(lv);
                                  lv = sv;
                            }      
                            if(((Slider*)(gp->actuator))->value() != sv)
                            {
                              //printf("PluginGui::updateControls slider\n");
                              
                              gp->label->blockSignals(true);
                              ((Slider*)(gp->actuator))->blockSignals(true);
                              ((Slider*)(gp->actuator))->setValue(sv);
                              gp->label->setValue(lv);
                              ((Slider*)(gp->actuator))->blockSignals(false);
                              gp->label->blockSignals(false);
                            } 
                          }
                          
                        }
                  else if (gp->type == GuiParam::GUI_SWITCH) {
                        if( plugin->controllerEnabled(i) && plugin->controllerEnabled2(i) )
                          {
                            bool v = (int)plugin->track()->pluginCtrlVal(genACnum(plugin->id(), i));
                            if(((CheckBox*)(gp->actuator))->isChecked() != v)
                            {
                              //printf("PluginGui::updateControls switch\n");
                              
                              ((CheckBox*)(gp->actuator))->blockSignals(true);
                              ((CheckBox*)(gp->actuator))->setChecked(v);
                              ((CheckBox*)(gp->actuator))->blockSignals(false);
                            } 
                          }
                        }
                  }
            }
      else if (gw) {
            for (int i = 0; i < nobj; ++i) {
                  QWidget* widget = gw[i].widget;
                  int type = gw[i].type;
                  int param = gw[i].param;
                  switch(type) {
                        case GuiWidgets::SLIDER:
                              if( plugin->controllerEnabled(param) && plugin->controllerEnabled2(param) )
                              {
                                double v = plugin->track()->pluginCtrlVal(genACnum(plugin->id(), param));
                                if(((Slider*)widget)->value() != v)
                                {
                                  //printf("PluginGui::updateControls slider\n");
                              
                                  ((Slider*)widget)->blockSignals(true);
                                  ((Slider*)widget)->setValue(v);
                                  ((Slider*)widget)->blockSignals(false);
                                }
                              }
                              break;
                        case GuiWidgets::DOUBLE_LABEL:
                              if( plugin->controllerEnabled(param) && plugin->controllerEnabled2(param) )
                              {
                                double v = plugin->track()->pluginCtrlVal(genACnum(plugin->id(), param));
                                if(((DoubleLabel*)widget)->value() != v)
                                {
                                  //printf("PluginGui::updateControls label\n");
                              
                                  ((DoubleLabel*)widget)->blockSignals(true);
                                  ((DoubleLabel*)widget)->setValue(v);
                                  ((DoubleLabel*)widget)->blockSignals(false);
                                }
                              }
                              break;
                        case GuiWidgets::QCHECKBOX:
                              if( plugin->controllerEnabled(param) && plugin->controllerEnabled2(param) )
                              { 
                                bool b = (bool) plugin->track()->pluginCtrlVal(genACnum(plugin->id(), param));
                                if(((QCheckBox*)widget)->isChecked() != b)
                                {
                                  //printf("PluginGui::updateControls checkbox\n");
                              
                                  ((QCheckBox*)widget)->blockSignals(true);
                                  ((QCheckBox*)widget)->setChecked(b);
                                  ((QCheckBox*)widget)->blockSignals(false);
                                } 
                              }
                              break;
                        case GuiWidgets::QCOMBOBOX:
                              if( plugin->controllerEnabled(param) && plugin->controllerEnabled2(param) )
                              { 
                                int n = (int) plugin->track()->pluginCtrlVal(genACnum(plugin->id(), param));
                                if(((QComboBox*)widget)->currentItem() != n)
                                {
                                  //printf("PluginGui::updateControls combobox\n");
                              
                                  ((QComboBox*)widget)->blockSignals(true);
                                  ((QComboBox*)widget)->setCurrentItem(n);
                                  ((QComboBox*)widget)->blockSignals(false);
                                } 
                              }
                              break;
                        }
                  }   
            }
      }

//---------------------------------------------------------
//   guiParamChanged
//---------------------------------------------------------

void PluginGui::guiParamChanged(int idx)
{
      QWidget* w = gw[idx].widget;
      int param  = gw[idx].param;
      int type   = gw[idx].type;

      AutomationType at = AUTO_OFF;
      AudioTrack* track = plugin->track();
      if(track)
        at = track->automationType();
      
      if(at == AUTO_WRITE || (audio->isPlaying() && at == AUTO_TOUCH))
        plugin->enableController(param, false);
      
      double val = 0.0;
      switch(type) {
            case GuiWidgets::SLIDER:
                  val = ((Slider*)w)->value();
                  break;
            case GuiWidgets::DOUBLE_LABEL:
                  val = ((DoubleLabel*)w)->value();
                  break;
            case GuiWidgets::QCHECKBOX:
                  val = double(((QCheckBox*)w)->isChecked());
                  break;
            case GuiWidgets::QCOMBOBOX:
                  val = double(((QComboBox*)w)->currentItem());
                  break;
            }

      for (int i = 0; i < nobj; ++i) {
            QWidget* widget = gw[i].widget;
            if (widget == w || param != gw[i].param)
                  continue;
            int type   = gw[i].type;
            switch(type) {
                  case GuiWidgets::SLIDER:
                        ((Slider*)widget)->setValue(val);
                        break;
                  case GuiWidgets::DOUBLE_LABEL:
                        ((DoubleLabel*)widget)->setValue(val);
                        break;
                  case GuiWidgets::QCHECKBOX:
                        ((QCheckBox*)widget)->setChecked(int(val));
                        break;
                  case GuiWidgets::QCOMBOBOX:
                        ((QComboBox*)widget)->setCurrentItem(int(val));
                        break;
                  }
            }
      
      int id = plugin->id();
      if(track && id != -1)
      {
          id = genACnum(id, param);
          
          // p3.3.43
          //audio->msgSetPluginCtrlVal(((PluginI*)plugin), id, val);
          
          //if(track)
          //{
            // p3.3.43
            audio->msgSetPluginCtrlVal(track, id, val);
            
            switch(type) 
            {
               case GuiWidgets::DOUBLE_LABEL:
               case GuiWidgets::QCHECKBOX:
                 track->startAutoRecord(id, val);
               break;
               default:
                 track->recordAutomation(id, val);
               break;  
            }  
          //}  
      } 
      plugin->setParam(param, val);
}

//---------------------------------------------------------
//   guiParamPressed
//---------------------------------------------------------

void PluginGui::guiParamPressed(int idx)
      {
      int param  = gw[idx].param;

      AutomationType at = AUTO_OFF;
      AudioTrack* track = plugin->track();
      if(track)
        at = track->automationType();
      
      if(at != AUTO_OFF)
        plugin->enableController(param, false);
      
      int id = plugin->id();
      if(!track || id == -1)
        return;
      
      id = genACnum(id, param);
      
      // NOTE: For this to be of any use, the freeverb gui 2142.ui
      //  would have to be used, and changed to use CheckBox and ComboBox
      //  instead of QCheckBox and QComboBox, since both of those would
      //  need customization (Ex. QCheckBox doesn't check on click).
      /*
      switch(type) {
            case GuiWidgets::QCHECKBOX:
                    double val = (double)((CheckBox*)w)->isChecked();
                    track->startAutoRecord(id, val);
                  break;
            case GuiWidgets::QCOMBOBOX:
                    double val = (double)((ComboBox*)w)->currentItem();
                    track->startAutoRecord(id, val);
                  break;
            }
      */      
      }

//---------------------------------------------------------
//   guiParamReleased
//---------------------------------------------------------

void PluginGui::guiParamReleased(int idx)
      {
      int param  = gw[idx].param;
      int type   = gw[idx].type;
      
      AutomationType at = AUTO_OFF;
      AudioTrack* track = plugin->track();
      if(track)
        at = track->automationType();
      
      // Special for switch - don't enable controller until transport stopped.
      if(at != AUTO_WRITE && (type != GuiWidgets::QCHECKBOX
          || !audio->isPlaying()
          || at != AUTO_TOUCH))
        plugin->enableController(param, true);
      
      int id = plugin->id();
      
      if(!track || id == -1)
        return;
      
      id = genACnum(id, param);
      
      // NOTE: For this to be of any use, the freeverb gui 2142.ui
      //  would have to be used, and changed to use CheckBox and ComboBox
      //  instead of QCheckBox and QComboBox, since both of those would
      //  need customization (Ex. QCheckBox doesn't check on click).
      /*
      switch(type) {
            case GuiWidgets::QCHECKBOX:
                    double val = (double)((CheckBox*)w)->isChecked();
                    track->stopAutoRecord(id, param);
                  break;
            case GuiWidgets::QCOMBOBOX:
                    double val = (double)((ComboBox*)w)->currentItem();
                    track->stopAutoRecord(id, param);
                  break;
            }
      */
      }

//---------------------------------------------------------
//   guiSliderPressed
//---------------------------------------------------------

void PluginGui::guiSliderPressed(int idx)
      {
      int param  = gw[idx].param;
      QWidget *w = gw[idx].widget;
      
      AutomationType at = AUTO_OFF;
      AudioTrack* track = plugin->track();
      if(track)
        at = track->automationType();
      
      int id = plugin->id();
      
      if(at == AUTO_WRITE || (at == AUTO_READ || at == AUTO_TOUCH))
        plugin->enableController(param, false);
      
      if(!track || id == -1)
        return;
      
      id = genACnum(id, param);
      
      double val = ((Slider*)w)->value();
      plugin->setParam(param, val);
      
      //audio->msgSetPluginCtrlVal(((PluginI*)plugin), id, val);
      // p3.3.43
      audio->msgSetPluginCtrlVal(track, id, val);
      
      track->startAutoRecord(id, val);
      
      // Needed so that paging a slider updates a label or other buddy control.
      for (int i = 0; i < nobj; ++i) {
            QWidget* widget = gw[i].widget;
            if (widget == w || param != gw[i].param)
                  continue;
            int type   = gw[i].type;
            switch(type) {
                  case GuiWidgets::SLIDER:
                        ((Slider*)widget)->setValue(val);
                        break;
                  case GuiWidgets::DOUBLE_LABEL:
                        ((DoubleLabel*)widget)->setValue(val);
                        break;
                  case GuiWidgets::QCHECKBOX:
                        ((QCheckBox*)widget)->setChecked(int(val));
                        break;
                  case GuiWidgets::QCOMBOBOX:
                        ((QComboBox*)widget)->setCurrentItem(int(val));
                        break;
                  }
            }
      }

//---------------------------------------------------------
//   guiSliderReleased
//---------------------------------------------------------

void PluginGui::guiSliderReleased(int idx)
      {
      int param  = gw[idx].param;
      QWidget *w = gw[idx].widget;
      
      AutomationType at = AUTO_OFF;
      AudioTrack* track = plugin->track();
      if(track)
        at = track->automationType();
      
      if(at != AUTO_WRITE || (!audio->isPlaying() && at == AUTO_TOUCH))
        plugin->enableController(param, true);
      
      int id = plugin->id();
      
      if(!track || id == -1)
        return;
      
      id = genACnum(id, param);
      
      double val = ((Slider*)w)->value();
      track->stopAutoRecord(id, val);
      }
    
//---------------------------------------------------------
//   guiSliderRightClicked
//---------------------------------------------------------

void PluginGui::guiSliderRightClicked(const QPoint &p, int idx)
{
  int param  = gw[idx].param;
  int id = plugin->id();
  if(id != -1)
    //song->execAutomationCtlPopup((AudioTrack*)plugin->track(), p, genACnum(id, param));
    song->execAutomationCtlPopup(plugin->track(), p, genACnum(id, param));
}

//---------------------------------------------------------
//   PluginWidgetFactory
//---------------------------------------------------------
#if 0 // ddskrjo
QWidget* PluginWidgetFactory::createWidget(const QString& className, QWidget* parent, const char* name) const
{
  if(className == "DoubleLabel")
    return new DoubleLabel(parent, name); 
  if(className == "Slider")
    return new Slider(parent, name); 
  return 0;  
};
#endif