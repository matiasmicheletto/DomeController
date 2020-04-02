#include <LiquidCrystal.h>
LiquidCrystal lcd(7, 8, 9, 10, 11, 12);

#define PIN_PUSH_BUTTON 13 // Pulsador
#define PIN_DET_RIGHT 2 // Pulso de rotacion derecho
#define PIN_DET_LEFT 3 // Pulso de rotacion izquierdo
#define PIN_DIRECTION_RELAY 4 // Giro izquierdo o derecho, convencion: HIGH -> Derecha
#define PIN_MOTOR_RELAY 5 // Encendido/apagado del motor
#define PIN_LED 6 // Led rojo
#define PIN_SWITCH A5 // Llave selectora

#define BAUDRATE 9600 // Comunicacion serie con la PC
#define NUMSLOTS 60 // Cantidad de marcas de la vuelta (idealmente par y divisor de 360)
#define MULT_360 6 // 360/NUMSLOTS
#define THRESHOLD_OFF 2 // Error maximo para detener motor
#define THRESHOLD_ON 4 // Error maximo para encender motor


enum LoopMode {AUTO, MANUAL};
enum MotorState {ON, OFF};
enum MotorDirection {LEFT, RIGHT};

LoopMode loopMode = MANUAL;
MotorState motorState = OFF;
MotorDirection motorDirection = LEFT;


int actualStep = 0; // Posicion actual de la cupula en ranuras
int setPointStep = 0; // Posicion deseada de la cupula en ranuras
int errorSteps = 0; // Senial de error
byte t500ms = 0, t1s = 0, t2s = 0; // Contadores para temporizado
boolean switchEnabled true; // Habilitacion del swich de control de la cupula

// Puntos cardinales
// {N, NNE, NE, ENE, E, ESE, SE, SSE, S, SSO, SO, OSO, O, ONO, NO, NNO};
// {0, 22.5, 45, 67.5, 90, 112.5, 135, 157.5, 180, 202.5, 225, 247.5, 270, 292.5, 315, 337.5};
 
void setup()
{
  Serial.begin(BAUDRATE);
  lcd.begin(20,4);
  
  pinMode(PIN_PUSH_BUTTON,INPUT);
  pinMode(PIN_DET_RIGHT,INPUT);
  pinMode(PIN_DET_LEFT,INPUT);
  pinMode(PIN_DIRECTION_RELAY,OUTPUT);
  pinMode(PIN_MOTOR_RELAY,OUTPUT);
  pinMode(PIN_LED,OUTPUT);
  pinMode(PIN_SWITCH,INPUT);


  noInterrupts();

  // PCI
  PCMSK0 |= (1 << PCINT5); // Pin D13
  PCIFR |= (1 << PCIF0); // Limpiar interrupciones
  PCICR |= (1 << PCIE0); // Habilitar PCI pines D8-D13
  attachInterrupt(0, pulseRight, RISING);
  attachInterrupt(1, pulseLeft, RISING);

  // TIMER 1 => temporizado
  TCCR1A = 0; // Normal operation
  TCCR1B = 0; // Normal operation, no clock source
  TCNT1 = 0; // Inicializar en 0
  OCR1A = 6250; // Registro de comparacion = 16MHz/256/10Hz 
  TCCR1B |= (1 << WGM12); // Modo CTC (Reiniciar luego de la comparacion)
  TCCR1B |= (1 << CS12); // 256 prescaler
  TIMSK1 |= (1 << OCIE1A); // Habilitar interr. por 

  interrupts();
}


void pulseRight()
// ISR: deteccion de pulso hacia la derecha
{
  actualStep = actualStep+1; // Hacia la derecha es incremento positivo
  if(actualStep > NUMSLOTS-1) // Si excede el numero de ranuras, 
    actualStep = actualStep - NUMSLOTS; // Descontar un giro
}


void pulseLeft()
// ISR: deteccion de pulso hacia la izquierda
{
  actualStep = actualStep-1; // Hacia la izquierda es incremento negativo
  if(actualStep < 0) // Si es un numero negativo,
    actualStep = NUMSLOTS + actualStep; // Agregar un giro para que sea positivo
}


ISR (PCINT0_vect) 
// ISR: cambio de estado del pin del pulsador
{
  if( digitalRead(PIN_PUSH_BUTTON) == HIGH ) // Verificar estado del pin    
  {  
  	loopMode = loopMode == AUTO ? MANUAL : AUTO; // Alternar valor de la variable
  	if( motorState == ON ) // Si el motor estaba encendido al momento de cambiar de modo, se apaga
  	{
  	  stopDome();
  	  switchEnabled = true; // Se rehabilita el control del switch (aunque no tiene efecto en modo auto)
  	}
}


ISR(TIMER1_COMPA_vect)
// ISR: Timer 1 (100 ms) temporizado
{
  t500ms++;  t1s++;  t2s++; // Contadores de temporizado

  if( t500ms > 4 ) 
  // Determinar senial de error cada medio segundo y apagar motor si esta en fase
  {  
    t500ms = 0; // Reiniciar contador
    int diff = setPointStep - actualStep; // Distancia hasta el setPoint
    if( diff > NUMSLOTS/2 ) // Si supera media vuelta hacia la derecha
      errorSteps = diff - NUMSLOTS; // Recorrer distancia hacia la izquierda
    else if( diff < - NUMSLOTS/2 ) // Si supera media vuelta hacia la izquierda
      errorSteps = diff + NUMSLOTS; // Recorrer distancia hacia la derecha
    else // Si no supera media vuelta, ir en la direccion del signo del error
      errorSteps = diff;

    if( loopMode == AUTO && abs(errorSteps) < THRESHOLD_OFF ) // En modo automatico, si el error es chico 
      stopDome(); // Apagar motor
  }

  if( t1s > 9 )
  // Refrescar display cada 1 segundo
  {
    t1s = 0;
    updateDisplay();
  }

  if( t2s > 19 )
  // Determinar si hay que encender el motor cada 2 segundos
  {
    t2s = 0; // Reiniciar contador
    if( loopMode == AUTO ) // En modo automatico
    {
      if( abs(errorSteps) > THRESHOLD_ON ) // Si el error es grande, encender motor
        if( errorSteps > 0 ) // Si error > 0,
          moveDomeRight(); // Mover cupula hacia la derecha
        else // Si error < 0,
          moveDomeLeft(); // Mover cupula hacia la izquierda
    }
    else if(switchEnabled) // Modo manual y llave habilitada
	{
      int val = analogRead(PIN_SWITCH); // Leer estado de la llave
      if( val > 450 && val < 550 ) // Si es derecha
        moveDomeRight(); // Encender derecha
      else if(val > 950) // Si no, si es izquierda
        moveDomeLeft(); // Encender izquierda
      else
        stopDome(); // Apagar motor
    }
  }
}


void serialEvent()
// ISR serie
{
  char c = (char) Serial.read(); // Caracter de control
  switch(c)
  {
  	case 'a': // Variables, formato:   "amodo pos sp motor\n", ejemplo: at 120 120 o\n
      Serial.print("a");
      if(loopMode == AUTO) Serial.print("t ");
      else Serial.print("m ");
      Serial.print( actualStep * MULT_360 ); 
      Serial.println( setPointStep * MULT_360 );
      if(motorState == OFF) Serial.print("s");
      else
      {
        if(motorDirection == RIGHT)
          Serial.println("r");
        else
          Serial.println("l");
      }
      break;
    case 'b': // Alternar moto automatico/manual
      loopMode = loopMode == AUTO ? MANUAL : AUTO;
      Serial.println("b"); // ACK
      break;
    case 'c':  // Mover hacia la derecha (solo para modo manual)
      if( loopMode == MANUAL ) // Si el modo manual no esta habilitado, no se puede controlar el motor
      {	
      	switchEnabled = false; // Deshabilitar el switch en control desde la pc
        moveDomeRight();
        Serial.println("c"); // ACK
      }
      else Serial.println("#"); // Se debe cambiar el modo
      break;
    case 'd': // Mover hacia la izquierda (solo para modo manual)
      if( loopMode == MANUAL ) // Si el modo manual no esta habilitado, no se puede controlar el motor
      {
      	switchEnabled = false; // Deshabilitar el switch en control desde la pc
        moveDomeLeft();
        Serial.println("d"); // ACK
      }
      else Serial.println("#"); // Se debe cambiar el modo
      break;
    case 'e': // Detener cupula (solo para modo manual)
      if( loopMode == MANUAL ) // Si el modo manual no esta habilitado, no se puede controlar el motor
      {  
      	stopDome();
      	switchEnabled = true; // Rehabilitar el switch en control desde la pc
      	Serial.println("e"); // ACK
      }
      else Serial.println("#"); // Se debe cambiar el modo
      break;
    case 'f': // Cambiar setpoint
      String arg = Serial.readStringUntil('\n'); // Leer argumento
      setPointStep = arg.toInt() / MULT_360; // Convertir a pasos
      Serial.println("f"); // ACK
      break;
    case 'g': // Actualizar posicion
      String arg = Serial.readStringUntil('\n'); // Leer argumento
      actualStep = arg.toInt() / MULT_360; // Convertir a pasos
      Serial.println("g"); // ACK
      break;
  }
}


void moveDomeRight()
// Encender motor hacia la derecha
{
  if( motorState == ON && motorDirection == LEFT ) // Si esta girando hacia la izquierda
    stopDome(); // Detener
  else
  {
    digitalWrite(PIN_DIRECTION_RELAY, HIGH);
    motorDirection = RIGHT;
    digitalWrite(PIN_MOTOR_RELAY, HIGH);
    motorState = ON;
  }
}


void moveDomeLeft()
// Encender motor hacia la izquierda o mantener girando hacia la izquierda
{
  if( motorState == ON && motorDirection == RIGHT ) // Si esta girando hacia la izquierda
    stopDome(); // Detener
  else
  {
    digitalWrite(PIN_DIRECTION_RELAY, LOW);
    motorDirection = LEFT;
    digitalWrite(PIN_MOTOR_RELAY, HIGH);
    motorState = ON;
  }
}


void stopDome()
// Detener motor
{
  digitalWrite(PIN_MOTOR_RELAY, LOW);
  motorState = OFF;
  digitalWrite(PIN_DIRECTION_RELAY, LOW); // Por defecto se apaga para que no consuma corriente
  motorDirection = LEFT;
}


void updateDisplay()
// Resfrescar el display
{
  // Mostrar modo
  lcd.setCursor(0,0);
  lcd.print("Modo: ");
  if(loopMode == AUTO) lcd.print("Automatico");
  else lcd.print("Manual    ");

  // Mostrar posicion
  lcd.setCursor(0,1);
  lcd.print("Posicion: "); 
  lcd.print( actualStep * MULT_360 );
  lcd.print("   ");

  // Mostrar setpoint
  lcd.setCursor(0,2);
  lcd.print("Setpoint: ");
  lcd.print( setPointStep * MULT_360 );
  lcd.print("   ");

  // Mostrar estado del motor
  lcd.setCursor(0,3);
  if(motorState == OFF)
    lcd.print("Detenido      ");
  else
  {
    if(motorDirection == RIGHT)
      lcd.print("Rot. derecha  ");
    else
      lcd.print("Rot. izquierda");
  }
}


void loop()
{

}
