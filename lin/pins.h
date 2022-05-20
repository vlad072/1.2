class Input { //=============== input ===============
  public:
    Input(uint8_t pin) {
      _pin = pin;
      pinMode(_pin, INPUT_PULLUP);
      _old = active();
    }
    bool active(void) {
      uint8_t _counter; do {
        _counter = 0;
        for (uint8_t _i = 0; _i < 32; _i++) _counter += (byte)digitalRead(_pin);
      } while ((_counter > 0) && (_counter < 32));
      return !_counter;
    }
    int8_t change() {
      bool _state = active();
      if (_state == _old) return -1;
      _old = _state;
      return (byte)_state;
    }
  private:
    uint8_t _pin; bool _old;
};
class Adc { //================= adc =================
  public:
    Adc(uint8_t pin, float k = 1) {
      _pin = pin; _k = k;
      //pinMode(_pin, INPUT); // HI-Z default. wrong for A6, A7!!!
    }
    int16_t value() {
      int16_t _readings[8] = {0,0,0,0,0,0,0,0};
      uint8_t _ranges[8]   = {0,1,2,3,4,5,6,7};
      for (uint8_t _readnum = 0; _readnum < 8; _readnum++) {
        _readings[_readnum] = analogRead(_pin);
        if (_readnum)
          for (uint8_t _prev = 0; _prev < _readnum; _prev++)
            if (abs(_readings[_readnum] - _readings[_prev]) < 5) {
              _ranges[_readnum] = _ranges[_prev];
              break;
            }
      }
      uint8_t _toprange = 0; uint8_t _topcount = 0;
      for (uint8_t _rangenum = 0; _rangenum < 8; _rangenum++) {
        uint8_t _counter = 0;
        for (uint8_t _readnum = 0; _readnum < 8; _readnum++)
          if (_ranges[_readnum] == _rangenum) _counter++;
        if (_counter > _topcount) _topcount = _counter, _toprange = _rangenum;
      }
      int16_t _sum  = 0;
      for (uint8_t _readnum = 0; _readnum < 8; _readnum++)
        if (_ranges[_readnum] == _toprange) _sum += _readings[_readnum];
      return round((float)_sum / (float)_topcount  * _k);
    }
    void reset(uint8_t pin, float k) {
      _pin = pin; _k = k;
    }
  private:
    uint8_t _pin; float _k;
};
class Output { //============== output ===============
  public:
    Output(uint8_t pin) {
      _pin = pin; _begin = 0; _timeout  = 0; _switches = 0;
      pinMode(_pin, OUTPUT); digitalWrite(_pin, LOW);
    }
    void set(bool val) {
      digitalWrite(_pin, val); _switches = 0;
    }
    void twitch(uint32_t timeout, uint8_t count = 1) {
      if (count < 1) return;
      digitalWrite(_pin, HIGH); _begin = millis(); _timeout = timeout; _switches = count*2-1;
    }
    bool on() {
      return digitalRead(_pin);
    }
    void processing() {
      if (_switches < 1) return;
      if ((millis() - _begin) >= _timeout) {
        digitalWrite(_pin, !digitalRead(_pin));
        _begin = millis();
        if (_switches > 0) _switches--; else _switches = 0;
      }
    }
  private:
    uint8_t  _pin;
    uint32_t _begin;
    uint32_t _timeout;
    byte     _switches;
};
class InOut : public Input, Output { //====== in/out =======
  public:
    InOut(uint8_t inpin, uint8_t outpin) : Input(inpin), Output(outpin) { }
    bool set(bool val) {
      Output::set(val);
      return active() == val;
    }
};
