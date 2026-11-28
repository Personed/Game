############################################################
#
#  GNU Makefile for $/Poseidon/lib
#
#  3.11.2002, PE
#
############################################################

#  etiquette:

.SUFFIXES:
.SUFFIXES: .cpp .c .o

#  compiler:

INCLUDES = -I$(ROOT) -I$(ROOT)/Poseidon -I. -ICloth -I$(ROOT)/Poseidon/extern -I$(ROOT)/Poseidon/extern/libcurl/include -I/usr/lib/jvm/java-6-openjdk-i386/include -I/usr/lib/jvm/java-6-openjdk-i386/include/linux

VPATH    = .:Network:Cloth:UI:GameSpy:GameSpy/common:GameSpy/qr2:GameSpy/serverbrowsing:GameSpy/natneg:GameSpy/gcdkey:GameSpy/pt:GameSpy/ghttp:Encryption:AI:Marina:KBExt:Shape:ASE:SimulWeather:TerSynth:../RV/KBBase:../../Es/Memory:../extern/Botan:../extern/libcurl/lib
PROJECT  = -D_SERVER=1

#  linker:

ifdef SUPER_RELEASE
  ADDLIBS = -lrt
else
  ADDLIBS = -lrt
endif


ifdef ICC
  LIB     = -L$(LIBTARGET) -L$(LIBTARGET)/../w3c-lib -L$(LIBTARGET)/../w3c-modules-lib -logg -lelement -lbotan-1.10 -lessence -lrsa -lstdc++ \
            -lz \
            -lm -ljvm -lpthread -L/opt/intel/compiler60/ia32/lib -lcxa
else
  ifdef REDHAT
    LIB   = -L$(LIBTARGET) -L$(LIBTARGET)/../w3c-lib -L$(LIBTARGET)/../w3c-modules-lib -logg -lelement -lbotan-1.10 -lessence \
            -L/usr/lib/x86_64-linux-gnu -lcurl \
            -lrsa -L/usr/local/lib -lstdc++ \
            -lz \
            -lm -ljvm -lpthread
  else
    LIB   = -L$(LIBTARGET) -L/usr/lib/jvm/java-6-openjdk-i386/jre/lib/i386/client \
            -lelement -lessence -lbotan-1.10 -lcurl -lrsa -lstdc++ \
            -lm -ljvm -lpthread -logg -ldl $(ADDLIBS)
  endif
endif

#  object files to build:

SOURCES =  registration.cpp wounds.cpp delayhlp.cpp drawText.cpp fastCap.cpp fileLocator.cpp mapObject.cpp  \
objectId.cpp serializeBinExt.cpp liveStats.cpp match.cpp netTransportNet.cpp  \
network.cpp networkMsg.cpp voiceDX8.cpp  \
voiceOAL.cpp voiceXA2.cpp arcadeTemplate.cpp location.cpp soundDX8obs.cpp  \
soundOAL.cpp soundScene.cpp soundSys.cpp soundXA2.cpp ClothObject.cpp  \
airplane.cpp animSource.cpp camera.cpp cameraHold.cpp car.cpp curve3D.cpp  \
detector.cpp door.cpp dynSound.cpp envTracker.cpp fsmEntity.cpp gearBox.cpp  \
head.cpp helicopter.cpp helicopterRTD.cpp house.cpp hudDesc.cpp identity.cpp laserTarget.cpp  \
motion.cpp motorcycle.cpp parachute.cpp person.cpp planeOrHeli.cpp  \
proxyWeapon.cpp randomShape.cpp recoil.cpp seaGull.cpp ship.cpp shots.cpp  \
simul.cpp smokes.cpp soldierOld.cpp tank.cpp tankOrCar.cpp target.cpp  \
thing.cpp tracks.cpp transport.cpp turret.cpp vehicleAI.cpp vehicleTypes.cpp  \
vehSupply.cpp camEffects.cpp dammage.cpp engine.cpp engineDraw.cpp  \
fileServerUseGameSettings.cpp fireplace.cpp font.cpp fontDraw.cpp  \
frame.cpp freeTrack.cpp freeTrackClient.cpp fsm.cpp geography.cpp  \
heads.cpp interpol.cpp invisibleVeh.cpp joystick.cpp LandLighting.cpp  \
landSave.cpp lights.cpp mainCommon.cpp mainInput.cpp npClientWraps.cpp  \
objectClasses.cpp objLine.cpp oggtext.cpp onExit.cpp onExitLib.cpp  \
pactext.cpp pathSteer.cpp roads.cpp scripts.cpp shapeLand.cpp  \
speaker.cpp titEffects.cpp trackIR.cpp txtBank.cpp txtPreload.cpp  \
viewer.cpp visibility.cpp weapons.cpp world.cpp wpch.cpp HUDInfo.cpp  \
inGameUI.cpp inGameUIDraw.cpp jpgImport.cpp mbcs.cpp paramFileExt.cpp  \
VBS1.cpp XOR1024.cpp gameStateExt.cpp vbsCmds.cpp stringtableExt.cpp  \
animation.cpp clipdraw.cpp clipShape.cpp collisions.cpp drawpoly.cpp  \
lockCache.cpp object.cpp rtAnimation.cpp scene.cpp shadow.cpp shapeDraw.cpp  \
stdLib.cpp transLight.cpp paramArchiveExt.cpp aiArcade.cpp aiCenter.cpp  \
aiGroup.cpp aiRadio.cpp aiSubgroup.cpp aiSubgroupFSM.cpp aiUnit.cpp  \
fsmScripted.cpp operMap.cpp pathPlanner.cpp suppressMap.cpp gameDirs.cpp  \
saveGame.cpp autoComplete.cpp chat.cpp contextMenu.cpp displayUI.cpp  \
dispMissionEditor.cpp menu.cpp missionDirs.cpp missionEditor.cpp  \
missionEditorCursor.cpp missionEditorExport.cpp optionsUI.cpp progressDisplay.cpp  \
uiArcade.cpp uiContainers.cpp uiControls.cpp uiControlsExt.cpp uiMap.cpp  \
uiMapExport.cpp uiMapExt.cpp uiViewport.cpp dispMissionEditorVBS.cpp  \
missionEditorCursorVBS.cpp missionEditorVBS.cpp reqObject.cpp carCollisions.cpp  \
dampers.cpp dantzig_old.cpp mtl_lu_solve.cpp IKengine.cpp kbExt.cpp  \
depMakeUseBinMake.cpp edges.cpp faceArray.cpp material.cpp modelConfig.cpp  \
plane.cpp poly.cpp shape.cpp shapeFile.cpp vertex.cpp editor.cpp timeManager.cpp  \
morale.cpp plugins.cpp spacemouse.cpp VBS2AES256.cpp landClutter.cpp  \
landscape.cpp landWeather.cpp SimulCloudRendererBase.cpp  \
SimulSkyRendererBase.cpp SimulWeatherBase.cpp  \
SimulWeatherMemAlloc.cpp SimulWeatherUtils.cpp terrain.cpp tersynth.cpp  \
tersynth_helpers.cpp memAllocCommon.cpp memGrow.cpp memNedAlloc.cpp  \
memTable.cpp \
kbCenter.cpp kbMessage.cpp kbTopic.cpp  \
engDummy.cpp main.cpp main3dfx.cpp useAppInfoOFP.cpp winapp.cpp \
camShake.cpp oalDummy.cpp \
networkClient.cpp networkServer.cpp \
pingLoc.cpp geoLoc.cpp httpRequest.cpp jvmInit.cpp progressScr.cpp

# include ../extern/libcurl/lib/Makefile.inc

CSOURCES = darray.c hashtable.c md5c.c gsAssert.c gsAvailable.c gsCrypt.c gsDebug.c  \
gsLargeInt.c gsMemory.c gsPlatform.c gsPlatformSocket.c gsPlatformThread.c  \
gsPlatformUtil.c gsRC4.c gsSHA1.c gsSSL.c gsStringUtil.c gsXML.c qr2.c  \
qr2regkeys.c sb_crypt.c sb_queryengine.c sb_server.c sb_serverbrowsing.c  \
sb_serverlist.c NATify.c natneg.c gcdkeyc.c gcdkeys.c ptMain.c ghttpBuffer.c  \
ghttpCallbacks.c ghttpCommon.c ghttpConnection.c ghttpEncryption.c  \
ghttpMain.c ghttpPost.c ghttpProcess.c ASEMasterSDK.c 


OBJS    = $(addprefix $(TARGET)/, $(SOURCES:.cpp=.o))
COBJS   = $(addprefix $(TARGET)/, $(CSOURCES:.c=.o))
ADDOBJS = $(LIBTARGET)/useMallocMemFunctions.o \
          #$(LIBTARGET)/paramFileUsePreprocC.o
LIBS    = $(LIBTARGET)/libessence.a \
          $(LIBTARGET)/libelement.a \
          $(LIBTARGET)/librsa.a \
          $(LIBTARGET)/libogg.a
DEPS    = $(SOURCES:.cpp=.d)
CDEPS   = $(CSOURCES:.c=.d)

BINARY = server$(TARGET)
ifdef DEBUG
  FINALIZE = touch
else
  FINALIZE = strip
endif

#  targets:

all:    Makefile $(BINARY)

Makefile: $(DEPS) $(CDEPS)
	$(TOUCH) $@

$(DEPS): %.d: %.cpp
	set -e; $(CCC) -M $(INCLUDES) $(CPPFLAGS) $(CPPONLYFLAGS) $(PROJECT) $< \
	    | sed 's^\($*\)\.o[ :]*^$(TARGET)/\1.o $@ : ^g' > $@; \
	    [ -s $@ ] || $(RM) $@

$(CDEPS): %.d: %.c
	set -e; $(CC) -M $(INCLUDES) $(CPPFLAGS) $(PROJECT) $< \
	    | sed 's^\($*\)\.o[ :]*^$(TARGET)/\1.o $@ : ^g' > $@; \
	    [ -s $@ ] || $(RM) $@

$(OBJS): $(TARGET)/%.o: %.cpp
	$(CCC) $(INCLUDES) $(CPPFLAGS) $(CPPONLYFLAGS) $(PROJECT) -c -o $@ $<

$(COBJS): $(TARGET)/%.o: %.c
	$(CC) $(INCLUDES) $(CPPFLAGS) $(PROJECT) -c -o $@ $<


# some exceptions (special compilation)
$(TARGET)/botan_all_linux.o: ../extern/Botan/botan_all_linux.cpp
	$(CCC) $(INCLUDES) $(CPPFLAGS) $(CPPONLYFLAGS) $(PROJECT) -fexceptions -c -o $@ $<

$(TARGET)/registration.o: registration.cpp
	$(CCC) $(INCLUDES) $(CPPFLAGS) $(CPPONLYFLAGS) $(PROJECT) -fexceptions -c -o $@ $<

$(TARGET)/httpRequest.o: httpRequest.cpp
	$(CCC) $(INCLUDES) $(CPPFLAGS) $(CPPONLYFLAGS) $(PROJECT) -fexceptions -c -o $@ $<


$(BINARY): $(LIBS) $(OBJS) $(COBJS) $(ADDOBJS)
	$(LD) $(LDFLAGS) $(OBJS) $(COBJS) $(ADDOBJS) $(LIB) -o $@
	$(FINALIZE) $@
	cp $@ $(ROOT)/bin/

.PHONY: clean

clean:
	$(RM) d/*.o r/*.o sr/*.o *.d

include $(DEPS) $(CDEPS)
