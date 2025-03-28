#include <cstdio>
#include "RRemote.h"

RRemote RRemote::remote;
RRemote::RemoteParams RRemote::params_;
bb::ConfigStorage::HANDLE RRemote::paramsHandle_;

RRemote::RRemote(): 
  mode_(MODE_REGULAR) {
  name_ = "remote";
  description_ = "Main subsystem for the BB8 remote";
  help_ = "Main subsystem for the BB8 remote"\
"Available commands:\r\n"\
"\tstatus                 Prints current status (buttons, axes, etc.)\r\n"\
"\trunning_status on|off  Continuously prints status\r\n"\
"\ttestsuite              Run test suite\r\n"\
"\tcalibrate_imu          Run IMU calibration\r\n"\
"\treset                  Factory reset\n"\
"\tset_droid ADDR         Set droid address to ADDR (64bit hex - max 16 digits, omit the 0x)\n"\
"\tset_other_remote ADDR  Set other remote address to ADDR (64bit hex - max 16 digits, omit the 0x)\n";

  started_ = false;
  operationStatus_ = RES_SUBSYS_NOT_STARTED;
  deltaR_ = 0; deltaP_ = 0; deltaH_ = 0;
  memset((void*)&lastPacketSent_, 0, sizeof(Packet));

  params_.droidAddress = {0,0};
  params_.otherRemoteAddress = {0,0};
  params_.config.leftIsPrimary = true;
  params_.config.ledBrightness = 7;
  params_.config.sendRepeats = 3;
  params_.config.lIncrRotBtn = RInput::BUTTON_4;
  params_.config.rIncrRotBtn = RInput::BUTTON_4;
  params_.config.lIncrTransBtn = RInput::BUTTON_NONE;
  params_.config.rIncrTransBtn = RInput::BUTTON_NONE;
  params_.config.deadbandPercent = 8;

  message_.setTitle("?");

  remoteVisL_.setRepresentsLeftRemote(true);
  remoteVisL_.setName("Left Remote");
  remoteVisR_.setRepresentsLeftRemote(false);
  remoteVisR_.setName("Right Remote");
  mainVis_.addWidget(&remoteVisL_);
  mainVis_.addWidget(&remoteVisR_);

  topLabel_.setSize(RDisplay::DISPLAY_WIDTH, RDisplay::CHAR_HEIGHT);
  topLabel_.setPosition(0, 0);
  topLabel_.setFrameType(RLabelWidget::FRAME_BOTTOM);
  topLabel_.setTitle("Top Label");

  bottomLabel_.setSize(RDisplay::DISPLAY_WIDTH, RDisplay::CHAR_HEIGHT+4);
  bottomLabel_.setPosition(0, RDisplay::DISPLAY_HEIGHT-RDisplay::CHAR_HEIGHT-3);
  bottomLabel_.setJustification(RLabelWidget::LEFT_JUSTIFIED, RLabelWidget::BOTTOM_JUSTIFIED);
  bottomLabel_.setFrameType(RLabelWidget::FRAME_TOP);
  bottomLabel_.setTitle("");

  leftSeqnum_.setSize(23, 8);
  leftSeqnum_.setPosition(1, RDisplay::DISPLAY_HEIGHT-RDisplay::CHAR_HEIGHT);
  leftSeqnum_.setFrameColor(RDisplay::LIGHTBLUE2);
  leftSeqnum_.setChar('L');
  rightSeqnum_.setSize(23, 8);
  rightSeqnum_.setPosition(leftSeqnum_.x()+leftSeqnum_.width()+4, leftSeqnum_.y());
  rightSeqnum_.setChar('R');
  droidSeqnum_.setSize(23, 8);
  droidSeqnum_.setPosition(rightSeqnum_.x()+rightSeqnum_.width()+4, leftSeqnum_.y());
  droidSeqnum_.setChar('D');

  dialog_.setTitle("Hi! :-)");
  dialog_.setValue(5);
  dialog_.setRange(0, 10);
}

Result RRemote::initialize() { 
  addParameter("led_brightness", "LED Brightness", ledBrightness_, 8);
  addParameter("deadband", "Joystick deadband in percent", deadbandPercent_, 15);
  addParameter("send_repeats", "Send repeats for control packets (0 = send only once)", sendRepeats_, 15);

  paramsHandle_ = ConfigStorage::storage.reserveBlock("remote", sizeof(params_), (uint8_t*)&params_);
	if(ConfigStorage::storage.blockIsValid(paramsHandle_)) {
    Console::console.printfBroadcast("Remote: Storage block 0x%x is valid.\n", paramsHandle_);
    ConfigStorage::storage.readBlock(paramsHandle_);
    Console::console.printfBroadcast("Other Remote Address: 0x%lx:%lx\n", params_.otherRemoteAddress.addrHi, params_.otherRemoteAddress.addrLo);
    Console::console.printfBroadcast("Droid address: 0x%lx:%lx\n", params_.droidAddress.addrHi, params_.droidAddress.addrLo);
  } else {
    Console::console.printfBroadcast("Remote: Storage block 0x%x is invalid, using initialized parameters.\n", paramsHandle_);
    ConfigStorage::storage.writeBlock(paramsHandle_);
  }

  RInput::input.setCalibration(params_.hCalib, params_.vCalib);
#if defined(LEFT_REMOTE)
  RInput::input.setIncrementalRot(RInput::Button(params_.config.lIncrRotBtn));
#else
  RInput::input.setIncrementalRot(RInput::Button(params_.config.rIncrRotBtn));
#endif
  deadbandPercent_ = params_.config.deadbandPercent;
  ledBrightness_ = params_.config.ledBrightness;
  sendRepeats_ = params_.config.sendRepeats;
  RInput::input.setDeadbandPercent(params_.config.deadbandPercent);
  RDisplay::display.setLEDBrightness(ledBrightness_<<2);

  if(params_.otherRemoteAddress.isZero()) rightSeqnum_.setFrameColor(RDisplay::LIGHTGREY);
  else rightSeqnum_.setFrameColor(RDisplay::LIGHTBLUE2);
  if(params_.droidAddress.isZero()) droidSeqnum_.setFrameColor(RDisplay::LIGHTGREY);
  else droidSeqnum_.setFrameColor(RDisplay::LIGHTBLUE2);

  return Subsystem::initialize();
}

Result RRemote::start(ConsoleStream *stream) {
  (void)stream;
  runningStatus_ = false;
  operationStatus_ = RES_OK;

  if(RInput::input.begin() == false) {
    LOG(LOG_FATAL, "Error initializing RInput\n");
  }

#if defined(LEFT_REMOTE)
  populateMenus();
  showMain();
  drawGUI();
  delay(10);
  mainWidget_->setNeedsFullRedraw();
  leftSeqnum_.setNeedsFullRedraw();
  droidSeqnum_.setNeedsFullRedraw();
  rightSeqnum_.setNeedsFullRedraw();
#endif

  started_ = true;

  return RES_OK;
}

Result RRemote::stop(ConsoleStream *stream) {
  (void)stream;
  RDisplay::display.setLED(RDisplay::LED_BOTH, RDisplay::LED_YELLOW);
  operationStatus_ = RES_OK;
  started_ = false;
  
  return RES_OK;
}

Result RRemote::step() {
  if(!started_) return RES_SUBSYS_NOT_STARTED;

  RInput::input.update();
  if(millis()-lastDroidMs_ > 500) droidSeqnum_.setNoComm(true);
  if(millis()-lastRightMs_ > 500) rightSeqnum_.setNoComm(true);

  if((bb::Runloop::runloop.getSequenceNumber() % 4) == 0) {
    if(runningStatus_) {
      printExtendedStatusLine();
    }
    fillAndSend();
  } else if((bb::Runloop::runloop.getSequenceNumber() % 4) == 1) {
    updateStatusLED();
    drawGUI();
  } else {
    RDisplay::display.setLED(RDisplay::LED_COMM, RDisplay::LED_OFF);
  }

  if((bb::Runloop::runloop.getSequenceNumber() % 10) == 0) {
    if(RInput::input.secondsSinceLastMotion() > 10) RDisplay::display.setLEDBrightness(1);
    else RDisplay::display.setLEDBrightness(params_.config.ledBrightness << 2);
  }

  return RES_OK;
}

void RRemote::parameterChangedCallback(const String& name) {
  if(name == "deadband") {
    params_.config.deadbandPercent = deadbandPercent_;
    RInput::input.setDeadbandPercent(deadbandPercent_);
    Console::console.printfBroadcast("Set deadband percent to %d\n", deadbandPercent_);
  } else if (name == "led_brightness") {
    params_.config.ledBrightness = ledBrightness_;
    RDisplay::display.setLEDBrightness(ledBrightness_<<2);
    Console::console.printfBroadcast("Set LED Brightness to %d\n", ledBrightness_);
  } else if(name == "send_repeats") {
    params_.config.sendRepeats = sendRepeats_;
    Console::console.printfBroadcast("Set send repeats to %d\n", sendRepeats_);
  }
}

void RRemote::setMainWidget(RWidget* widget) {
  widget->setPosition(RDisplay::MAIN_X, RDisplay::MAIN_Y);
  widget->setSize(RDisplay::MAIN_WIDTH, RDisplay::MAIN_HEIGHT);
  RInput::input.clearCallbacks();
  widget->takeInputFocus();
  widget->setNeedsFullRedraw();
  widget->setNeedsContentsRedraw();
  setTopTitle(widget->name());
  mainWidget_ = widget;
}

void RRemote::showMain() {
  setMainWidget(&mainVis_);
  RInput::input.setConfirmShortPressCallback([=]{RRemote::remote.showMenu(&mainMenu_);});
}

void RRemote::showMenu(RMenuWidget* menu) {
  setMainWidget(menu);
  menu->resetCursor();
}

void RRemote::showPairDroidMenu() {
  showMessage("Please wait");
  
  pairDroidMenu_.clear();

  Console::console.printfBroadcast("Discovering nodes...\n");
  Result res = XBee::xbee.discoverNodes(discoveredNodes_);
  if(res != RES_OK) {
    Console::console.printfBroadcast("%s\n", errorMessage(res));
    return;
  }
  Console::console.printfBroadcast("%d nodes discovered.\n", discoveredNodes_.size());

  pairDroidMenu_.clear();
  int num = 0;
  for(auto& n: discoveredNodes_) {
    if(XBee::stationTypeFromId(n.stationId) != XBee::STATION_DROID) continue;
    Console::console.printfBroadcast("Station \"%s\", ID 0x%x\n", n.name, n.stationId);
    pairDroidMenu_.addEntry(n.name, [=]() { RRemote::remote.selectDroid(n.address); showMain(); });
    num++;
  }
  pairDroidMenu_.setName(String(num) + " Droids");
  pairDroidMenu_.addEntry("<--", [=]() { RRemote::remote.showMenu(&pairMenu_); });

  Console::console.printfBroadcast("Showing droidsMenu\n");
  showMenu(&pairDroidMenu_);
}

void RRemote::showPairRemoteMenu() {
  showMessage("Please wait");

  pairRemoteMenu_.clear();

  Result res = XBee::xbee.discoverNodes(discoveredNodes_);
  if(res != RES_OK) {
    Console::console.printfBroadcast("%s\n", errorMessage(res));
    showMenu(&pairMenu_);
    return;
  }

  int num = 0;
  for(auto& n: discoveredNodes_) {
    if(XBee::stationTypeFromId(n.stationId) != XBee::STATION_REMOTE) continue;
    pairRemoteMenu_.addEntry(n.name, [=]() { RRemote::remote.selectRightRemote(n.address); showMain(); });
    num++;
  }
  pairRemoteMenu_.addEntry("<--", [=]() { RRemote::remote.showMenu(&pairMenu_); });
  pairRemoteMenu_.setName(String(num) + " Remotes");

  showMenu(&pairRemoteMenu_);
}

void RRemote::populateMenus() {
#if defined(LEFT_REMOTE)
  mainMenu_.setName("Main Menu");
  pairMenu_.setName("Pair");
  leftRemoteMenu_.setName("Left Remote");
  rightRemoteMenu_.setName("Right Remote");
  bothRemotesMenu_.setName("Both Remotes");
  droidMenu_.setName("Droid");
  
  mainMenu_.clear();
  if(!params_.droidAddress.isZero()) mainMenu_.addEntry("Droid...", [=]() { showMenu(&droidMenu_); });
  mainMenu_.addEntry("Left Remote...", [=]() { showMenu(&leftRemoteMenu_); });
  if(!params_.otherRemoteAddress.isZero()) mainMenu_.addEntry("Right Remote...", [=]() { showMenu(&rightRemoteMenu_); });
  mainMenu_.addEntry("Both Remotes...", [=](){showMenu(&bothRemotesMenu_);});
  mainMenu_.addEntry("Pair...", [=]() { RRemote::remote.showMenu(&pairMenu_); });
  mainMenu_.addEntry("<--", []() { RRemote::remote.showMain(); });
  
  pairMenu_.clear();
  pairMenu_.addEntry("Right Remote...", []() { RRemote::remote.showPairRemoteMenu(); });
  pairMenu_.addEntry("Droid...", []() { RRemote::remote.showPairDroidMenu(); });
  pairMenu_.addEntry("<--", [=]() { RRemote::remote.showMenu(&mainMenu_); });

  droidMenu_.clear();
  droidMenu_.addEntry("<--", [=]() {showMenu(&mainMenu_);});

  lRIncrRotMenu_.clear();
  lRIncrRotMenu_.setName("Incr Rotation");
  lRIncrRotMenu_.addEntry("Disable", [=]{setIncrRotButtonCB(RInput::BUTTON_NONE, true);showMain();}, RInput::BUTTON_NONE);
  lRIncrRotMenu_.addEntry("Button 1", [=]{setIncrRotButtonCB(RInput::BUTTON_1, true);showMain();}, RInput::BUTTON_1);
  lRIncrRotMenu_.addEntry("Button 2", [=]{setIncrRotButtonCB(RInput::BUTTON_2, true);showMain();}, RInput::BUTTON_2);
  lRIncrRotMenu_.addEntry("Button 3", [=]{setIncrRotButtonCB(RInput::BUTTON_3, true);showMain();}, RInput::BUTTON_3);
  lRIncrRotMenu_.addEntry("Button 4", [=]{setIncrRotButtonCB(RInput::BUTTON_4, true);showMain();}, RInput::BUTTON_4);
  lRIncrRotMenu_.addEntry("<--", [=]{showMenu(&leftRemoteMenu_);});
  lRIncrRotMenu_.highlightWidgetsWithTag(params_.config.lIncrRotBtn);

  rRIncrRotMenu_.clear();
  rRIncrRotMenu_.setName("Incr Rotation");
  rRIncrRotMenu_.addEntry("Disable", [=]{setIncrRotButtonCB(RInput::BUTTON_NONE, false);showMain();}, RInput::BUTTON_NONE);
  rRIncrRotMenu_.addEntry("Button 1", [=]{setIncrRotButtonCB(RInput::BUTTON_1, false);showMain();}, RInput::BUTTON_1);
  rRIncrRotMenu_.addEntry("Button 2", [=]{setIncrRotButtonCB(RInput::BUTTON_2, false);showMain();}, RInput::BUTTON_2);
  rRIncrRotMenu_.addEntry("Button 3", [=]{setIncrRotButtonCB(RInput::BUTTON_3, false);showMain();}, RInput::BUTTON_3);
  rRIncrRotMenu_.addEntry("Button 4", [=]{setIncrRotButtonCB(RInput::BUTTON_4, false);showMain();}, RInput::BUTTON_4);
  rRIncrRotMenu_.addEntry("<--", [=]{showMenu(&rightRemoteMenu_);});
  rRIncrRotMenu_.highlightWidgetsWithTag(params_.config.rIncrRotBtn);

  leftRemoteMenu_.clear();
  leftRemoteMenu_.addEntry("Incr Rot...", [=]{showMenu(&lRIncrRotMenu_);});
  leftRemoteMenu_.addEntry("Calib Joystick", [=]{startCalibrationCB(true);});
  leftRemoteMenu_.addEntry("Set Primary", [=]{setLeftIsPrimaryCB(true);showMain();}, params_.config.leftIsPrimary?1:0);
  leftRemoteMenu_.addEntry("Factory Reset", [=]{factoryResetCB(true);});
  leftRemoteMenu_.addEntry("<--", [=]() {showMenu(&mainMenu_);});
  leftRemoteMenu_.highlightWidgetsWithTag(1);

  rightRemoteMenu_.clear();
  rightRemoteMenu_.addEntry("Incr Rot...", [=]{showMenu(&rRIncrRotMenu_);});
  rightRemoteMenu_.addEntry("Calib Joystick", [=]{startCalibrationCB(false);});
  rightRemoteMenu_.addEntry("Set Primary", [=]{setLeftIsPrimaryCB(false);showMain();}, params_.config.leftIsPrimary?0:1);
  rightRemoteMenu_.addEntry("Factory Reset", [=]() {factoryResetCB(false);});
  rightRemoteMenu_.addEntry("<--", [=]() {showMenu(&mainMenu_);});
  rightRemoteMenu_.highlightWidgetsWithTag(1);

  bothRemotesMenu_.clear();
  bothRemotesMenu_.addEntry("LED Level", [=]{showLEDBrightnessDialog();});
  bothRemotesMenu_.addEntry("Joy Deadband", [=]{showJoyDeadbandDialog();});
  bothRemotesMenu_.addEntry("Send Repeats", [=]{showSendRepeatsDialog();});
  bothRemotesMenu_.addEntry("<--", [=]() {showMenu(&mainMenu_);});
#endif // LEFT_REMOTE
}

void RRemote::drawGUI() {
  topLabel_.draw();
  bottomLabel_.draw();
  leftSeqnum_.draw();
  rightSeqnum_.draw();
  droidSeqnum_.draw();
  if(dialogActive_) dialog_.draw();
  else if(mainWidget_ != NULL) mainWidget_->draw();
  if(needsMenuRebuild_) {
    populateMenus();
    needsMenuRebuild_ = false;
  }
}

void RRemote::setTopTitle(const String& title) {
  topLabel_.setTitle(title);
}

void RRemote::selectDroid(const HWAddress& droid) {
#if defined(LEFT_REMOTE)
  Console::console.printfBroadcast("Selecting droid 0x%lx:%lx\n", droid.addrHi, droid.addrLo);
  Runloop::runloop.excuseOverrun();
  
  params_.droidAddress = droid;
  
  ConfigPacket packet;
  ConfigPacket::ConfigReplyType reply;
  bool showSuccess = true;

  packet.type = bb::ConfigPacket::CONFIG_SET_LEFT_REMOTE_ID;
  packet.cfgPayload.address = XBee::xbee.hwAddress();
  Result res = XBee::xbee.sendConfigPacket(params_.droidAddress, PACKET_SOURCE_LEFT_REMOTE, packet, reply, sequenceNumber(), true);
  if(res != RES_OK) {
    showMessage(String("L ID -> D:\n") + bb::errorMessage(res), MSGDELAY, RDisplay::LIGHTRED2);
    return;
  } else if(reply != ConfigPacket::CONFIG_REPLY_OK) {
    showMessage(String("L ID -> D:\nError ") + int(reply), MSGDELAY, RDisplay::LIGHTRED2);
    return;
  }

  if(params_.otherRemoteAddress.addrHi != 0 || params_.otherRemoteAddress.addrLo != 0) {
    packet.type = bb::ConfigPacket::CONFIG_SET_DROID_ID;
    packet.cfgPayload.address = params_.droidAddress;
    Result res = XBee::xbee.sendConfigPacket(params_.otherRemoteAddress, PACKET_SOURCE_LEFT_REMOTE, packet, reply, sequenceNumber(), true);
    if(res != RES_OK) {
      showMessage(String("D ID -> R:\n") + bb::errorMessage(res), MSGDELAY, RDisplay::LIGHTRED2);
      showSuccess = false;
    } else if(reply != ConfigPacket::CONFIG_REPLY_OK) {
      showMessage(String("D ID -> R:\nError ") + int(reply), MSGDELAY, RDisplay::LIGHTRED2);
      showSuccess = false;
    }
  } 
        
  if(showSuccess) showMessage("Success", MSGDELAY, RDisplay::LIGHTGREEN2);
  droidSeqnum_.setFrameColor(RDisplay::LIGHTBLUE2);
  ConfigStorage::storage.writeBlock(paramsHandle_);
  needsMenuRebuild_ = true;
#endif
}

void RRemote::selectRightRemote(const HWAddress& address) {
#if defined(LEFT_REMOTE)
  Console::console.printfBroadcast("Selecting right remote 0x%lx:%lx\n", address.addrHi, address.addrLo);
  Runloop::runloop.excuseOverrun();

  params_.otherRemoteAddress = address;

  ConfigPacket packet;
  ConfigPacket::ConfigReplyType reply;
  bool showSuccess = true;

  // Set left remote address to right remote
  packet.type = bb::ConfigPacket::CONFIG_SET_LEFT_REMOTE_ID;
  packet.cfgPayload.address = XBee::xbee.hwAddress();
  Result res = XBee::xbee.sendConfigPacket(params_.otherRemoteAddress, PACKET_SOURCE_LEFT_REMOTE, packet, reply, sequenceNumber(), true);
  if(res != RES_OK) {
    showMessage(String("L ID -> R:\n") + bb::errorMessage(res), MSGDELAY, RDisplay::LIGHTRED2);
    return;
  }
  if(reply != ConfigPacket::CONFIG_REPLY_OK) {
    showMessage(String("L ID -> R:\nError ") + int(reply), MSGDELAY, RDisplay::LIGHTRED2);
    return;
  }

  // Set droid address to right remote
  if(!params_.droidAddress.isZero()) {
    packet.type = bb::ConfigPacket::CONFIG_SET_DROID_ID;
    packet.cfgPayload.address = params_.droidAddress;
    Result res = XBee::xbee.sendConfigPacket(params_.otherRemoteAddress, PACKET_SOURCE_LEFT_REMOTE, packet, reply, sequenceNumber(), true);
    if(res != RES_OK) {
      showMessage(String("D ID -> R:\n") + bb::errorMessage(res), MSGDELAY, RDisplay::LIGHTRED2);
      showSuccess = false;
    }
    if(reply != ConfigPacket::CONFIG_REPLY_OK) {
      showMessage(String("D ID -> R:\nError ") + int(reply), MSGDELAY, RDisplay::LIGHTRED2);
      showSuccess = false;
    }
  }

  if(showSuccess) showMessage("Success", MSGDELAY, RDisplay::LIGHTGREEN2);
  rightSeqnum_.setFrameColor(RDisplay::LIGHTBLUE2);
  ConfigStorage::storage.writeBlock(paramsHandle_);
  needsMenuRebuild_ = true;
#endif
}

void RRemote::updateStatusLED() {
  if(mode_ == MODE_CALIBRATION) {
    RDisplay::display.setLED(RDisplay::LED_STATUS, RDisplay::LED_BLUE);
  } else if(Console::console.isStarted() && XBee::xbee.isStarted() && isStarted() && RInput::input.imuOK() && RInput::input.mcpOK()) {
    if(RInput::input.joyAtZero())
      RDisplay::display.setLED(RDisplay::LED_STATUS, RDisplay::LED_WHITE);
    else
      RDisplay::display.setLED(RDisplay::LED_STATUS, RDisplay::LED_GREEN);
  } else if(!XBee::xbee.isStarted() || !RInput::input.imuOK() || !RInput::input.mcpOK() ) {
    RDisplay::display.setLED(RDisplay::LED_STATUS, RDisplay::LED_RED);
  } else {
    RDisplay::display.setLED(RDisplay::LED_STATUS, RDisplay::LED_YELLOW);
  }
}

void RRemote::setIncrRotButtonCB(RInput::Button button, bool left) {
#if defined(LEFT_REMOTE)
  RInput::Button tempBtn;
  if(left) {
    if(params_.config.lIncrRotBtn == button) return;
    RInput::input.setIncrementalRot(button);
    params_.config.lIncrRotBtn = button;
    lRIncrRotMenu_.highlightWidgetsWithTag(button);
    ConfigStorage::storage.writeBlock(paramsHandle_);
  } else {
    if(params_.config.rIncrRotBtn == button) return;
    tempBtn = RInput::Button(params_.config.rIncrRotBtn);
    params_.config.rIncrRotBtn = button;
    rRIncrRotMenu_.highlightWidgetsWithTag(button);
    if(sendConfigToRightRemote() != RES_OK) {
      params_.config.rIncrRotBtn = tempBtn;
    } else {
      ConfigStorage::storage.writeBlock(paramsHandle_);
    }
  }
  needsMenuRebuild_ = true;
#else
  Console::console.printfBroadcast("setIncrRotButtonCB() is left remote only!\n");
#endif
}


void RRemote::factoryResetCB(bool left) {
#if defined(LEFT_REMOTE)
  if(left) {
    factoryReset();
  } else {
    Console::console.printfBroadcast("Factory reset right remote!\n");
    bb::Packet packet(bb::PACKET_TYPE_CONFIG, bb::PACKET_SOURCE_LEFT_REMOTE, sequenceNumber());
    packet.payload.config.type = bb::ConfigPacket::CONFIG_FACTORY_RESET;
    packet.payload.config.cfgPayload.magic = bb::ConfigPacket::MAGIC;
    XBee::xbee.sendTo(params_.otherRemoteAddress, packet, true);

    params_.otherRemoteAddress = {0, 0};
    ConfigStorage::storage.writeBlock(paramsHandle_);
    rightSeqnum_.setNoComm(true);
    rightSeqnum_.setFrameColor(RDisplay::LIGHTGREY);
    needsMenuRebuild_ = true;
  }
#else
  Console::console.printfBroadcast("setIncrRotButtonCB() is left remote only!\n");
#endif
}

void RRemote::startCalibrationCB(bool left) {
#if defined(LEFT_REMOTE)
  if(left) {
    startCalibration();
  } else {
    ConfigPacket packet;
    ConfigPacket::ConfigReplyType reply;
    packet.type = bb::ConfigPacket::CONFIG_CALIBRATE;
    packet.cfgPayload.magic = bb::ConfigPacket::MAGIC;

    Result res = XBee::xbee.sendConfigPacket(params_.otherRemoteAddress, PACKET_SOURCE_LEFT_REMOTE, packet, reply, sequenceNumber(), true);
    if(res != RES_OK) {
      showMessage(String("Calib -> R:\n") + bb::errorMessage(res), MSGDELAY, RDisplay::LIGHTRED2);
      showMain();
      return;
    } 
    if(reply != ConfigPacket::CONFIG_REPLY_OK) {
      showMessage(String("Calib -> R:\nError ") + int(reply), MSGDELAY, RDisplay::LIGHTRED2);
      showMain();
      return;
    }

    mainVis_.showIndex(1);
    remoteVisR_.crosshair().setMinMax(1024, 4096-1024, 1024, 4096-1024);
    remoteVisR_.crosshair().showMinMaxRect();
    remoteVisR_.crosshair().setMinMaxRectColor(RDisplay::RED);
    showMain();
  }
#else
  Console::console.printfBroadcast("startCalibrationCB() is left remote only!\n");
#endif
}

void RRemote::finishCalibrationCB(bool left) {
#if defined(LEFT_REMOTE)
  Runloop::runloop.excuseOverrun();
  if(left) {
    showMain();
    RInput::input.setConfirmShortPressCallback([=]{RRemote::remote.showMenu(&mainMenu_);});
    remoteVisL_.crosshair().showMinMaxRect(false);

    finishCalibration();
  } else {
    remoteVisR_.crosshair().showMinMaxRect(false);
  }
#else
  if(!left) {
    RInput::input.clearCallbacks();
    finishCalibration();
  }
#endif
}

void RRemote::setLeftIsPrimaryCB(bool yesno) {
  bool oldLIP = params_.config.leftIsPrimary;
  if(oldLIP == yesno) return;

  params_.config.leftIsPrimary = yesno;

#if defined(LEFT_REMOTE)
  Result res = sendConfigToRightRemote();
  if(res != RES_OK) {
    params_.config.leftIsPrimary = oldLIP;

    return;
  }
#endif
  
  ConfigStorage::storage.writeBlock(paramsHandle_);
  needsMenuRebuild_ = true;
}

void RRemote::factoryReset() {
  bb::ConfigStorage::storage.factoryReset();
  showMessage("Please restart");
  int i=0, dir=1;
  while(true) {
    RDisplay::display.setLED(RDisplay::LED_BOTH, 0, 0, i);
    i = i+dir;
    if(dir > 0 && i>=255) dir = -1;
    if(dir < 0 && i<=0) dir = 1;
    delay(10);
  }
}

void RRemote::startCalibration() {
#if defined(LEFT_REMOTE)
  mainVis_.showIndex(0);
  setMainWidget(&mainVis_);

  setTopTitle("Calibrating");
  remoteVisL_.crosshair().setMinMax(RInput::input.minJoyRawH, RInput::input.maxJoyRawH,
                                    RInput::input.minJoyRawV, RInput::input.maxJoyRawV);
  remoteVisL_.crosshair().showMinMaxRect();
  remoteVisL_.crosshair().setMinMaxRectColor(RDisplay::RED);
  RInput::input.setAllCallbacks([=]{finishCalibrationCB(true);});
#else // LEFT_REMOTE
  RInput::input.setAllCallbacks([=]{finishCalibration();});
#endif
  RInput::input.setCalibration(RInput::AxisCalib(), RInput::AxisCalib());
  mode_ = MODE_CALIBRATION;
}

void RRemote::finishCalibration() {
    uint16_t hMin = RInput::input.minJoyRawH;
    uint16_t hMax = RInput::input.maxJoyRawH;
    uint16_t vMin = RInput::input.minJoyRawV;
    uint16_t vMax = RInput::input.maxJoyRawV;
    uint16_t hCur = RInput::input.joyRawH;
    uint16_t vCur = RInput::input.joyRawV;

    Console::console.printfBroadcast("Hor: [%d..%d..%d] Ver: [%d..%d..%d] \n", hMin, hCur, hMax, vMin, vCur, vMax);
    if(hMin < 800 && hMax > 4096-800 && vMin < 800 && vMax > 4096-800) { 
      // accept calibration
      params_.hCalib.min = hMin;
      params_.hCalib.max = hMax;
      params_.hCalib.center = hCur;
      params_.vCalib.min = vMin;
      params_.vCalib.max = vMax;
      params_.vCalib.center = vCur;
      RInput::input.setCalibration(params_.hCalib, params_.vCalib);
      
      ConfigStorage::storage.writeBlock(paramsHandle_);

      RDisplay::display.flashLED(RDisplay::LED_BOTH, 2, 250, 250, 0, 150, 0);
    } else {
      // Values out of acceptable bounds - reject calibration.
      RDisplay::display.flashLED(RDisplay::LED_BOTH, 2, 250, 250, 150, 0, 0);
    }
    mode_ = MODE_REGULAR;
}

void RRemote::showMessage(const String& msg, unsigned int delayms, uint8_t color) {
  message_.setTitle(msg);
  message_.setForegroundColor(color);
  message_.setFrameColor(color);
  message_.draw();
  if(delayms != 0) delay(delayms);
}

void RRemote::showDialog() {
  dialogActive_ = true;
  dialog_.setNeedsFullRedraw();
  dialog_.takeInputFocus();
}

void RRemote::hideDialog() {
  dialogActive_ = false;
  mainWidget_->setNeedsFullRedraw();
  mainWidget_->takeInputFocus();
}

#if defined(LEFT_REMOTE)

void RRemote::showLEDBrightnessDialog() {
  dialog_.setTitle("LED Level");
  dialog_.setValue(params_.config.ledBrightness);
  dialog_.setSuffix("");
  dialog_.setRange(0, 7);
  dialog_.setOKCallback([=](int brt){setLEDBrightness(brt);});
  showDialog();
}

void RRemote::setLEDBrightness(uint8_t brt) {
  if(brt > 7) brt = 7;
  if(brt == params_.config.ledBrightness) return;
  params_.config.ledBrightness = ledBrightness_ = brt;
  RDisplay::display.setLEDBrightness(brt << 2);
  sendConfigToRightRemote();
  storeParams();
}

void RRemote::showJoyDeadbandDialog() {
  dialog_.setTitle("Joy Deadb.");
  dialog_.setValue(params_.config.deadbandPercent);
  dialog_.setSuffix("%");
  dialog_.setRange(0, 15);
  dialog_.setOKCallback([=](int db){setJoyDeadband(db);});
  showDialog();
}

void RRemote::setJoyDeadband(uint8_t db) {
  if(db > 15) db = 15;
  if(db == params_.config.deadbandPercent) return;
  params_.config.deadbandPercent = deadbandPercent_ = db;
  RInput::input.setDeadbandPercent(db);
  sendConfigToRightRemote();
  storeParams();
}

void RRemote::showSendRepeatsDialog() {
  dialog_.setTitle("Send Reps");
  dialog_.setValue(params_.config.sendRepeats);
  dialog_.setSuffix("");
  dialog_.setRange(0, 7);
  dialog_.setOKCallback([=](int sr){setSendRepeats(sr);});
  showDialog();
}

void RRemote::setSendRepeats(uint8_t sr) {
  if(sr > 7) sr = 7;
  if(sr == params_.config.sendRepeats) return;
  params_.config.sendRepeats = sendRepeats_ = sr;
  sendConfigToRightRemote();
  storeParams();
}

Result RRemote::sendConfigToRightRemote() {
  if(params_.otherRemoteAddress.isZero()) {
    LOG(LOG_WARN, "Trying to send to right remote but address is 0\n");
    return RES_CONFIG_INVALID_HANDLE;
  }

  ConfigPacket packet;
  ConfigPacket::ConfigReplyType reply;
  packet.type = bb::ConfigPacket::CONFIG_SET_REMOTE_PARAMS;
  packet.cfgPayload.remoteConfig = params_.config;

  Result res = XBee::xbee.sendConfigPacket(params_.otherRemoteAddress, PACKET_SOURCE_LEFT_REMOTE, packet, reply, sequenceNumber(), true);
  if(res != RES_OK) {
    showMessage(String("Config -> R:\n") + bb::errorMessage(res), MSGDELAY, RDisplay::LIGHTRED2);
    return res;
  } 
  if(reply != ConfigPacket::CONFIG_REPLY_OK) {
    showMessage(String("Config -> R:\nError ") + int(reply), MSGDELAY, RDisplay::LIGHTRED2);
    return RES_SUBSYS_COMM_ERROR;
  }

  showMessage("Success", MSGDELAY, RDisplay::LIGHTGREEN2);
  ConfigStorage::storage.writeBlock(paramsHandle_);
  return RES_OK;
}
#endif

bb::Result RRemote::fillAndSend() {
#if defined(LEFT_REMOTE)
  Packet packet(PACKET_TYPE_CONTROL, PACKET_SOURCE_LEFT_REMOTE, sequenceNumber());
#else
  Packet packet(PACKET_TYPE_CONTROL, PACKET_SOURCE_RIGHT_REMOTE, sequenceNumber());
#endif
  if(params_.config.leftIsPrimary == true) {
#if defined(LEFT_REMOTE)
    packet.payload.control.primary = true;
#else
    packet.payload.control.primary = false;
#endif
  } else {
#if defined(LEFT_REMOTE)
    packet.payload.control.primary = false;
#else
    packet.payload.control.primary = true;
#endif
  }

  RInput::input.fillControlPacket(packet.payload.control);
  remoteVisL_.visualizeFromPacket(packet.payload.control);
  leftSeqnum_.setSquareColor(packet.seqnum%8, RDisplay::GREEN);
  leftSeqnum_.setSquareColor((packet.seqnum+1)%8, leftSeqnum_.backgroundColor());

  Result res = RES_OK;
  int r=0, g=0, b=0;

  if((params_.droidAddress.addrLo != 0 || params_.droidAddress.addrHi != 0) && 
      (params_.otherRemoteAddress.addrHi != 0 || params_.otherRemoteAddress.addrLo != 0)) {        // both set -- white
    r = 255; g = 255; b = 255;
  } else if(params_.droidAddress.addrHi != 0 || params_.droidAddress.addrLo != 0) {                                    // only droid set -- blue
    r = 0; g = 0; b = 255;
  } else if(params_.otherRemoteAddress.addrHi != 0 || params_.otherRemoteAddress.addrLo != 0) {
    r = 255; g = 0; b = 255;
  }

#if !defined(LEFT_REMOTE) // right remote sends to left remote
  if(!params_.otherRemoteAddress.isZero()) {
    res = bb::XBee::xbee.sendTo(params_.otherRemoteAddress, packet, false);
    if(res != RES_OK) Console::console.printfBroadcast("%s\n", errorMessage(res));
  }
#endif

  // both remotes send to droid
  if(!params_.droidAddress.isZero() && mode_ == MODE_REGULAR) {
    //Console::console.printfBroadcast("Sending %d times to 0x%lx:%lx\n", params_.config.sendRepeats+1, params_.droidAddress.addrHi, params_.droidAddress.addrLo);
    for(int i=0; i<params_.config.sendRepeats+1; i++) {
      res = bb::XBee::xbee.sendTo(params_.droidAddress, packet, false);
    }
    if(res != RES_OK) {
      r = 255; g = 0; b = 0;
    }
  }

  RDisplay::display.setLED(RDisplay::LED_COMM, r, g, b);

  return res;
}

Result RRemote::handleConsoleCommand(const std::vector<String>& words, ConsoleStream *stream) {
  if(words.size() == 0) return RES_CMD_UNKNOWN_COMMAND;
  if(words[0] == "running_status") {
    if(words.size() != 2) return RES_CMD_INVALID_ARGUMENT_COUNT;
    if(words[1] == "on" || words[1] == "true") {
      runningStatus_ = true;
      return RES_OK;
    } else if(words[1] == "off" || words[1] == "false") {
      runningStatus_ = false;
      return RES_OK;
    }
    return RES_CMD_INVALID_ARGUMENT;
  }

  else if(words[0] == "calibrate") {
    startCalibration();

    return RES_OK;
  }

  else if(words[0] == "calibrate_imu") {
    RInput::input.imu().calibrate();
    return RES_OK;
  }

  else if(words[0] == "reset") {
    factoryReset();
    return RES_OK;
  }

  else if(words[0] == "set_droid") {
    if(words.size() != 2) return RES_CMD_INVALID_ARGUMENT_COUNT;
    if(words[1].length() > 16) return RES_CMD_INVALID_ARGUMENT_COUNT;
    uint64_t addr = 0;
    String addrstr = words[1];
    for(int i=0; i<addrstr.length(); i++) {
      if(i!=0) addr <<= 4;
      if(addrstr[i] >= '0' && addrstr[i] <= '9') addr = addr + (addrstr[i]-'0');
      else if(addrstr[i] >= 'a' && addrstr[i] <= 'f') addr = addr + (addrstr[i]-'a') + 0xa;
      else if(addrstr[i] >= 'A' && addrstr[i] <= 'F') addr = addr + (addrstr[i]-'a') + 0xa;
      else {
        stream->printf("Invalid character '%c' at position %d - must be 0-9a-fA-F.\n");
        return RES_CMD_INVALID_ARGUMENT;
      }
    }
    params_.droidAddress = {uint32_t(addr>>32), uint32_t(addr&0xffffffff)};
    stream->printf("Setting droid address to 0x%lx:%lx.\n", params_.droidAddress.addrHi, params_.droidAddress.addrLo);
    return RES_OK;
  }

  else if(words[0] == "set_other_remote") {
    if(words.size() != 2) return RES_CMD_INVALID_ARGUMENT_COUNT;
    if(words[1].length() > 16) return RES_CMD_INVALID_ARGUMENT_COUNT;
    uint64_t addr = 0;
    String addrstr = words[1];
    for(int i=0; i<addrstr.length(); i++) {
      if(i!=0) addr <<= 4;
      if(addrstr[i] >= '0' && addrstr[i] <= '9') addr = addr + (addrstr[i]-'0');
      else if(addrstr[i] >= 'a' && addrstr[i] <= 'f') addr = addr + (addrstr[i]-'a');
      else if(addrstr[i] >= 'A' && addrstr[i] <= 'F') addr = addr + (addrstr[i]-'a');
      else {
        stream->printf("Invalid character '%c' at position %d - must be 0-9a-fA-F.\n");
        return RES_CMD_INVALID_ARGUMENT;
      }
    }
    params_.otherRemoteAddress = {uint32_t(addr>>32), uint32_t(addr&0xffffffff)};
    stream->printf("Setting other remote address to 0x%lx:%lx.\n", params_.otherRemoteAddress.addrHi, params_.otherRemoteAddress.addrLo);
    return RES_OK;
  }

  else if(words[0] == "testsuite") {
    runTestsuite();
    return RES_OK;
  }

  return Subsystem::handleConsoleCommand(words, stream);
} 

Result RRemote::incomingControlPacket(const HWAddress& srcAddr, PacketSource source, uint8_t rssi, uint8_t seqnum, const ControlPacket& packet) {
#if defined(LEFT_REMOTE)
  if(source == PACKET_SOURCE_RIGHT_REMOTE) { // FIXME Must check for address too but we're getting 16bit addressed packets here?!
      remoteVisR_.visualizeFromPacket(packet);
      if(packet.button5 == true ||
         packet.button6 == true ||
         packet.button7 == true) {
          remoteVisR_.crosshair().showMinMaxRect(false);
      }
      
      rightSeqnum_.setSquareColor(seqnum%8, RDisplay::GREEN);
      rightSeqnum_.setSquareColor((seqnum+1)%8, rightSeqnum_.backgroundColor());
      rightSeqnum_.setNoComm(false);
      
      uint8_t diff = WRAPPEDDIFF(seqnum, lastRightSeqnum_, 8);
      if(diff>1) {
        Console::console.printfBroadcast("Seqnum expected: %d, received: %d, missed %d\n", lastRightSeqnum_+1, seqnum, diff-1);
        for(int i=1; i<diff; i++) rightSeqnum_.setSquareColor(lastRightSeqnum_+i, RDisplay::RED);
      }
      lastRightSeqnum_ = seqnum;
      lastRightMs_ = millis();
    
      return RES_OK;    
  } else {
    Console::console.printfBroadcast("Unknown address 0x%lx:%lx sent Control packet to left remote\n", srcAddr.addrHi, srcAddr.addrLo);
    return RES_SUBSYS_COMM_ERROR;
  }
#else
  Console::console.printfBroadcast("Address 0x%lx:%lx sent Control packet to right remote - should never happen\n", srcAddr.addrHi, srcAddr.addrLo);
  return RES_SUBSYS_COMM_ERROR;
#endif
}

Result RRemote::incomingStatePacket(const HWAddress& srcAddr, PacketSource source, uint8_t rssi, uint8_t seqnum, const StatePacket& packet) {
#if defined(LEFT_REMOTE)
  //Console::console.printfBroadcast("Got state packet from 0x%lx:%lx\n", srcAddr.addrHi, srcAddr.addrLo);
  if(source == PACKET_SOURCE_DROID) {
    //droidVis_.visualizeFromPacket(packet);

    uint8_t expected = (lastDroidSeqnum_+1)%8;
    droidSeqnum_.setSquareColor(seqnum%8, RDisplay::GREEN);
    droidSeqnum_.setSquareColor((seqnum+1)%8, droidSeqnum_.backgroundColor());
    droidSeqnum_.setNoComm(false);

    if(seqnum != expected) {
        int missed;
        if(expected < seqnum) missed = seqnum - expected;
        else missed = 8 + (seqnum - expected);
        Console::console.printfBroadcast("Seqnum expected: %d, received: %d, missed %d\n", expected, seqnum, missed);
        for(int i=1; i<missed; i++) droidSeqnum_.setSquareColor(lastRightSeqnum_+i, RDisplay::RED);
      }
    
    lastDroidSeqnum_ = seqnum;
    lastDroidMs_ = millis();

    return RES_OK;    
  }

  Console::console.printfBroadcast("Unknown address 0x%lx:%lx sent State packet to left remote\n", srcAddr.addrHi, srcAddr.addrLo);
  return RES_SUBSYS_COMM_ERROR;
#else
  Console::console.printfBroadcast("Address 0x%lx:%lx sent State packet to right remote - should never happen\n", srcAddr.addrHi, srcAddr.addrLo);
  return RES_SUBSYS_COMM_ERROR;
#endif
}

Result RRemote::incomingConfigPacket(const HWAddress& srcAddr, PacketSource source, uint8_t rssi, uint8_t seqnum, const ConfigPacket& packet) {
#if defined(LEFT_REMOTE)
  Console::console.printfBroadcast("Address 0x%lx:%lx sent Config packet to left remote - should never happen\n", srcAddr.addrHi, srcAddr.addrLo);
  return RES_SUBSYS_COMM_ERROR;
#else
  Console::console.printfBroadcast("Got config packet from 0x%llx, type %d\n", srcAddr, packet.type);
  if(srcAddr == params_.otherRemoteAddress || params_.otherRemoteAddress.isZero()) { // if we're not bound, we accept config packets from anyone
    switch(packet.type) {
    case bb::ConfigPacket::CONFIG_SET_LEFT_REMOTE_ID:
      if(packet.cfgPayload.address != srcAddr) {
        Console::console.printfBroadcast("Error: Pairing packet source 0x%lx:%lx and payload 0x%lx:%lx don't match\n", 
          srcAddr.addrHi, srcAddr.addrLo, packet.cfgPayload.address.addrHi, packet.cfgPayload.address.addrLo);
        return RES_SUBSYS_COMM_ERROR;
      }

      Console::console.printfBroadcast("Setting Left Remote ID to 0x%lx:%lx.\n", packet.cfgPayload.address.addrHi, packet.cfgPayload.address.addrLo);
      params_.otherRemoteAddress = packet.cfgPayload.address;
      ConfigStorage::storage.writeBlock(paramsHandle_);
      return RES_OK; 

    case bb::ConfigPacket::CONFIG_SET_DROID_ID:
      Console::console.printfBroadcast("Setting Droid ID to 0x%lx:%lx.\n", packet.cfgPayload.address.addrHi, packet.cfgPayload.address.addrLo);
      params_.droidAddress = packet.cfgPayload.address;
      ConfigStorage::storage.writeBlock(paramsHandle_);
      return RES_OK; 

    case bb::ConfigPacket::CONFIG_SET_REMOTE_PARAMS:
      Console::console.printfBroadcast("Setting remote params: LPrimary %d LED %d Repeats %d LIncrR %d RIncrR %d LIncrT %d RIncrT %d\n", 
                                       packet.cfgPayload.remoteConfig.leftIsPrimary, packet.cfgPayload.remoteConfig.ledBrightness, 
                                       packet.cfgPayload.remoteConfig.sendRepeats,
                                       packet.cfgPayload.remoteConfig.lIncrRotBtn, packet.cfgPayload.remoteConfig.rIncrRotBtn, 
                                       packet.cfgPayload.remoteConfig.lIncrTransBtn, packet.cfgPayload.remoteConfig.rIncrTransBtn);
      params_.config = packet.cfgPayload.remoteConfig;
      RInput::input.setIncrementalRot(RInput::Button(params_.config.rIncrRotBtn));
      ConfigStorage::storage.writeBlock(paramsHandle_);
      return RES_OK; 

    case bb::ConfigPacket::CONFIG_FACTORY_RESET:
      if(packet.cfgPayload.magic == bb::ConfigPacket::MAGIC) { // checks out
        factoryReset();
        return RES_OK; // HA! This never returns! NEVER! Hahahahahahaaaaa!
      }
      Console::console.printfBroadcast("Got factory reset packet but Magic 0x%llx doesn't check out!\n", packet.cfgPayload.magic);
      return RES_SUBSYS_COMM_ERROR;

    case bb::ConfigPacket::CONFIG_CALIBRATE:
      if(packet.cfgPayload.magic == bb::ConfigPacket::MAGIC) { // checks out
        startCalibration();
        return RES_OK;
      }
      Console::console.printfBroadcast("Got factory reset packet but Magic 0x%llx doesn't check out!\n", packet.cfgPayload.magic);
      return RES_SUBSYS_COMM_ERROR;

    default:
      Console::console.printfBroadcast("Unknown config packet type 0x%x.\n", packet.type);
      return RES_SUBSYS_COMM_ERROR;
    }
  } else {
    Console::console.printfBroadcast("Config packet from unknown source\n");
  }
  Console::console.printfBroadcast("Should never get here.\n");
  return RES_SUBSYS_COMM_ERROR;
#endif
}

String RRemote::statusLine() {
  String str = bb::Subsystem::statusLine() + ", ";
  if(RInput::input.imuOK()) str += "IMU OK, ";
  else str += "IMU error, ";
  if(RInput::input.mcpOK()) str += "Buttons OK.";
  else str += "Buttons error.";

  return str;
}

void RRemote::printExtendedStatus(ConsoleStream* stream) {
  Runloop::runloop.excuseOverrun();

  stream->printf("Sequence number: %ld\n", seqnum_);
  stream->printf("Addressing:\n");
  stream->printf("\tThis remote:  0x%lx:%lx\n", XBee::xbee.hwAddress().addrHi, XBee::xbee.hwAddress().addrLo);
  stream->printf("\tOther remote: 0x%lx:%lx\n", params_.otherRemoteAddress.addrHi, params_.otherRemoteAddress.addrLo);
  stream->printf("\tDroid:        0x%lx:%lx\n", params_.droidAddress.addrHi, params_.droidAddress.addrLo);
  stream->printf("Joystick:\n");
  stream->printf("\tHor: Raw %d\tnormalized %.2f\tcalib [%4d..%4d..%4d]\n", RInput::input.joyRawH, RInput::input.joyH, RInput::input.hCalib.min, RInput::input.hCalib.center, RInput::input.hCalib.max);
  stream->printf("\tVer: Raw %d\tnormalized %.2f\tcalib [%4d..%4d..%4d]\n", RInput::input.joyRawV, RInput::input.joyV, RInput::input.vCalib.min, RInput::input.vCalib.center, RInput::input.vCalib.max);

  if(RInput::input.imuOK()) {
    float pitch, roll, heading, rax, ray, raz, ax, ay, az;
    RInput::input.imu().getFilteredPRH(pitch, roll, heading);
    RInput::input.imu().getAccelMeasurement(rax, ray, raz);
    RInput::input.imu().getGravCorrectedAccel(ax, ay, az);
    stream->printf("IMU: OK\n");
    stream->printf("\tRotation             Pitch: %.2f Roll: %.2f Heading: %.2f\n", pitch, roll, heading);
    stream->printf("\tRaw Acceleration     X:%f Y:%f Z:%f\n", rax, ray, raz);
    stream->printf("\tGrav-corrected accel X:%f Y:%f Z:%f\n", ax, ay, az);
  } else {
    stream->printf("IMU: Error\n");
  }

  if(RInput::input.mcpOK()) {      
    stream->printf("Buttons: 1:%c 2:%c 3:%c 4:%c Joy:%c Confirm:%c Left:%c Right:%c\n",
                  RInput::input.buttons[RInput::BUTTON_1] ? 'X' : '_',
                  RInput::input.buttons[RInput::BUTTON_2] ? 'X' : '_',
                  RInput::input.buttons[RInput::BUTTON_3] ? 'X' : '_',
                  RInput::input.buttons[RInput::BUTTON_4] ? 'X' : '_',
                  RInput::input.buttons[RInput::BUTTON_JOY] ? 'X' : '_',
                  RInput::input.buttons[RInput::BUTTON_CONFIRM] ? 'X' : '_',
                  RInput::input.buttons[RInput::BUTTON_LEFT] ? 'X' : '_',
                  RInput::input.buttons[RInput::BUTTON_RIGHT] ? 'X' : '_');
  } else {
    stream->printf("Buttons: Error\n");
  }

  stream->printf("Potentiometer 1: %.1f\nPotentiometer 2: %.1f\n", RInput::input.pot1, RInput::input.pot2);
  stream->printf("Battery: %.1f\n", RInput::input.battery);
}

void RRemote::printExtendedStatusLine(ConsoleStream *stream) {
  const unsigned int bufsize = 255;
  char buf[bufsize];
  memset(buf, 0, bufsize);

  float pitch, roll, heading, rax, ray, raz, ax, ay, az;
  RInput::input.imu().getFilteredPRH(pitch, roll, heading);
  RInput::input.imu().getAccelMeasurement(rax, ray, raz);
  RInput::input.imu().getGravCorrectedAccel(ax, ay, az);

#if 0
  snprintf(buf, bufsize, "S%ld AX%f AY%f AZ%f\n", seqnum_, ax, ay, az);
#else
  snprintf(buf, bufsize, "S%ld H%d [%d..%d..%d] %f V%d [%d..%d..%d] %f P%.1f R%.1f H%.1f AX%.2f AY%.2f AZ%.2f P1%.1f P2%.1f Batt%.1f B%c%c%c%c%c%c%c%c",
    seqnum_,
    RInput::input.joyRawH, RInput::input.hCalib.min, RInput::input.hCalib.center, RInput::input.hCalib.max, RInput::input.joyH,
    RInput::input.joyRawV, RInput::input.vCalib.min, RInput::input.vCalib.center, RInput::input.vCalib.max, RInput::input.joyV,
    pitch, roll, heading,
    ax, ay, az,
    RInput::input.pot1, RInput::input.pot2,
    RInput::input.battery,
    RInput::input.buttons[RInput::BUTTON_1] ? '_' : 'X',
    RInput::input.buttons[RInput::BUTTON_2] ? '_' : 'X',
    RInput::input.buttons[RInput::BUTTON_3] ? '_' : 'X',
    RInput::input.buttons[RInput::BUTTON_4] ? '_' : 'X',
    RInput::input.buttons[RInput::BUTTON_JOY] ? '_' : 'X',
    RInput::input.buttons[RInput::BUTTON_CONFIRM] ? '_' : 'X',
    RInput::input.buttons[RInput::BUTTON_LEFT] ? '_' : 'X',
    RInput::input.buttons[RInput::BUTTON_RIGHT] ? '_' : 'X');
#endif
  if(stream)
    stream->printf("%s\n", buf);
  else 
    Console::console.printfBroadcast("%s\n", buf);
}

void RRemote::runTestsuite() {
  RInput::input.testMatrix();
}
