#ifndef IR_CONTROL_LITE_H
#define IR_CONTROL_LITE_H

class IRControlLite {
public:
  IRControlLite(int pin);
  void iniciar();
  uint32_t procesar();

  uint32_t getUltimoComando();  

private:
  int pin;
  uint32_t ultimoCodigo = 0;
  unsigned long ultimoTiempo = 0,ultimoTiempoGral = 0,t_pulsacion=50;
};

#endif
