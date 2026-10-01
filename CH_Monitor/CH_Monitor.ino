#include <LiquidCrystal_I2C.h>  // LCD I2C
#include <RTClib.h>             // Relógio em tempo real
#include <Wire.h>               // Comunicação I2C
#include <EEPROM.h>
#include "DHT.h"

// CONFIGURAÇÕES GERAIS
#define LOG_OPTION    0   // 1 = imprime o log continuamente no loop (ou envie 'L' pela Serial)
#define SERIAL_OPTION 0   // 1 = envia o horário atual continuamente pela Serial
#define UTC_OFFSET    0   // Ajuste de fuso horário (em horas)

// Sensor DHT (DHT11 na placa real, DHT22 no Wokwi)
#define DHTPIN  2
#define DHTTYPE DHT11     // NÃO ESQUECER DE ALTERAR
DHT dht(DHTPIN, DHTTYPE);


// LOGO (CARACTERES PERSONALIZADOS)
// C superior
byte C_top[8] = {
    B01111,
    B10000,
    B10000,
    B10000,
    B10000,
    B10000,
    B10000,
    B01111
};

// C inferior
byte C_bot[8] = {
    B01111,
    B10000,
    B10000,
    B10000,
    B10000,
    B10000,
    B10000,
    B01111
};

// H superior
byte H_top[8] = {
    B10001,
    B10001,
    B10001,
    B11111,
    B11111,
    B10001,
    B10001,
    B10001
};

// H inferior
byte H_bot[8] = {
    B10001,
    B10001,
    B10001,
    B11111,
    B11111,
    B10001,
    B10001,
    B10001
};


// HARDWARE
// Controle do reset por botão
unsigned long inicioPressaoReset = 0;

// Display LCD I2C (endereço: 0x27 ou 0x3F)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Relógio em tempo real
RTC_DS1307 RTC;

// Joystick e sensores
#define JOY_Y  A1
#define JOY_SW 6
#define LDRPIN A0

// Buzzer
#define BUZZER_PIN 8

// Limites fixos de luminosidade para o alarme
const int trigger_l_min = 100;  // Abaixo disso: muito escuro
const int trigger_l_max = 900;  // Acima disso: muito claro

// LED RGB
#define PIN_RGB_R 9   // Vermelho
#define PIN_RGB_G 10  // Verde
#define PIN_RGB_B 11  // Azul


// ESTADOS E IDIOMAS
enum SistemaState {
    TELA_IDIOMA,
    CONFIG_TEMP_MIN,
    CONFIG_TEMP_MAX,
    CONFIG_UMID_MIN,
    CONFIG_UMID_MAX,
    TELA_PRINCIPAL,
    DISPLAY_OFF
};
SistemaState estadoAtual = TELA_IDIOMA;

enum Language {
    PORTUGUES,
    ESPANHOL,
    INGLES
};
Language idiomaAtual = PORTUGUES;
int idiomaSelecionado = 0;


// FUNÇÕES DE HARDWARE
// Define a cor do LED RGB
void setCorRGB(int r, int g, int b)
{
    analogWrite(PIN_RGB_R, r);
    analogWrite(PIN_RGB_G, g);
    analogWrite(PIN_RGB_B, b);
}

// Reinicia o Arduino se o botão ficar pressionado por 5 segundos
void verificarReset()
{
    if (pressionado(JOY_SW))
    {
        // Começou a pressionar
        if (inicioPressaoReset == 0)
        {
            inicioPressaoReset = millis();
        }

        // Ficou pressionado pelo tempo necessário
        if (millis() - inicioPressaoReset >= 5000)
        {
            // Buzzer avisa que vai reiniciar
            tone(BUZZER_PIN, 2000);
            delay(500);
            noTone(BUZZER_PIN);

            // Pequena pausa antes do reset
            delay(100);

            // Reinicia o Arduino
            void (*resetArduino)(void) = 0;
            resetArduino();
        }
    }
    else
    {
        // Soltou antes do tempo
        inicioPressaoReset = 0;
    }
}

// Leitura do botão (pull-up: aciona em LOW)
bool pressionado(int botao)
{
    return digitalRead(botao) == LOW;
}

// Leitura do eixo Y do joystick
int lerJoystick()
{
    int valor = analogRead(JOY_Y);

    if (valor < 400) return -1;  // Para cima
    if (valor > 600) return 1;   // Para baixo
    return 0;                    // Parado
}


// CONFIGURAÇÕES DA EEPROM
const uint16_t EEPROM_MAGIC = 0x414C;
const int ADDR_MAGIC = 0;
const int ADDR_T_MIN = 2;
const int ADDR_T_MAX = 6;
const int ADDR_U_MIN = 10;
const int ADDR_U_MAX = 14;

const int maxRecords = 100;
const int recordSize = 8;

const int startAddress = 20;
const int endAddress = startAddress + (maxRecords * recordSize);
int currentAddress = startAddress;

int lastLoggedMinute = -1;
unsigned long ultimaTrocaLCD = 0;
int telaLCD = 0;

// Limites de alarme
float trigger_t_min = 20.0;
float trigger_t_max = 30.0;
float trigger_u_min = 30.0;
float trigger_u_max = 60.0;

// Protótipos
void get_log();
void getNextAddress();
void telaIdioma();
void salvarTriggersEEPROM();
void carregarTriggersEEPROM();
void exibirTelaConfiguracao();


// GERENCIAMENTO DA EEPROM
void salvarTriggersEEPROM()
{
    EEPROM.put(ADDR_MAGIC, EEPROM_MAGIC);
    EEPROM.put(ADDR_T_MIN, trigger_t_min);
    EEPROM.put(ADDR_T_MAX, trigger_t_max);
    EEPROM.put(ADDR_U_MIN, trigger_u_min);
    EEPROM.put(ADDR_U_MAX, trigger_u_max);
}

void carregarTriggersEEPROM()
{
    uint16_t magic = 0;
    EEPROM.get(ADDR_MAGIC, magic);

    if (magic == EEPROM_MAGIC) {
        EEPROM.get(ADDR_T_MIN, trigger_t_min);
        EEPROM.get(ADDR_T_MAX, trigger_t_max);
        EEPROM.get(ADDR_U_MIN, trigger_u_min);
        EEPROM.get(ADDR_U_MAX, trigger_u_max);
    } else {
        // EEPROM sem dados válidos: grava os valores padrão
        trigger_t_min = 20.0;
        trigger_t_max = 30.0;
        trigger_u_min = 30.0;
        trigger_u_max = 60.0;
        salvarTriggersEEPROM();
    }
}


// TEXTOS E CONVERSÕES
float getTemperaturaExibicao(float temperaturaC)
{
    if (idiomaAtual == INGLES) {
        return (temperaturaC * 9.0 / 5.0) + 32.0;
    }
    return temperaturaC;
}

String getTempUnit()
{
    return (idiomaAtual == INGLES) ? "F" : "C";
}

String txtTemp()
{
    return "Temp";
}

String txtHumidity()
{
    switch (idiomaAtual) {
        case PORTUGUES: return "Umid";
        case ESPANHOL:  return "Humed";
        case INGLES:    return "Hum";
    }
    return "";
}

String txtDate()
{
    switch (idiomaAtual) {
        case PORTUGUES: return "DATA";
        case ESPANHOL:  return "FECHA";
        case INGLES:    return "DATE";
    }
    return "";
}

String txtTime()
{
    switch (idiomaAtual) {
        case PORTUGUES: return "HORA";
        case ESPANHOL:  return "HORA";
        case INGLES:    return "TIME";
    }
    return "";
}

String txtLight()
{
    switch (idiomaAtual) {
        case PORTUGUES: return "Luminosidade";
        case ESPANHOL:  return "Luminosidad";
        case INGLES:    return "Light";
    }
    return "";
}


// TELAS DO LCD
void telaIdioma()
{
    lcd.clear();
    lcd.setCursor(0, 0);
    switch (idiomaAtual) {
        case ESPANHOL: lcd.print("Seleccione:"); break;
        case INGLES:   lcd.print("Select:");     break;
        default:       lcd.print("Selecione:");  break;
    }

    lcd.setCursor(0, 1);
    switch (idiomaSelecionado) {
        case 0: lcd.print("> Portugues"); break;
        case 1: lcd.print("> Espanol");   break;
        case 2: lcd.print("> English");   break;
    }
}

void exibirTelaConfiguracao()
{
    lcd.clear();
    lcd.setCursor(0, 0);

    switch (estadoAtual) {
        case CONFIG_TEMP_MIN:
            if (idiomaAtual == INGLES) lcd.print("Min Temp:");
            else lcd.print("Temp Minima:");
            lcd.setCursor(0, 1);
            lcd.print("> ");
            lcd.print(getTemperaturaExibicao(trigger_t_min), 1);
            lcd.print(" "); lcd.print(getTempUnit());
            break;

        case CONFIG_TEMP_MAX:
            if (idiomaAtual == INGLES) lcd.print("Max Temp:");
            else lcd.print("Temp Maxima:");
            lcd.setCursor(0, 1);
            lcd.print("> ");
            lcd.print(getTemperaturaExibicao(trigger_t_max), 1);
            lcd.print(" "); lcd.print(getTempUnit());
            break;

        case CONFIG_UMID_MIN:
            if (idiomaAtual == ESPANHOL) lcd.print("Humed Minima:");
            else if (idiomaAtual == INGLES) lcd.print("Min Humidity:");
            else lcd.print("Umid Minima:");
            lcd.setCursor(0, 1);
            lcd.print("> "); lcd.print(trigger_u_min, 1); lcd.print(" %");
            break;

        case CONFIG_UMID_MAX:
            if (idiomaAtual == ESPANHOL) lcd.print("Humed Maxima:");
            else if (idiomaAtual == INGLES) lcd.print("Max Humidity:");
            else lcd.print("Umid Maxima:");
            lcd.setCursor(0, 1);
            lcd.print("> "); lcd.print(trigger_u_max, 1); lcd.print(" %");
            break;

        default:
            break;
    }
}


// SETUP
void setup()
{
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(JOY_SW, INPUT_PULLUP);

    // Buzzer
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);

    // LED RGB (inicia apagado)
    pinMode(PIN_RGB_R, OUTPUT);
    pinMode(PIN_RGB_G, OUTPUT);
    pinMode(PIN_RGB_B, OUTPUT);
    setCorRGB(0, 0, 0);

    dht.begin();
    Serial.begin(9600);

    lcd.init();
    lcd.backlight();

    // Carrega os caracteres personalizados
    lcd.createChar(0, C_top);
    lcd.createChar(1, C_bot);
    lcd.createChar(2, H_top);
    lcd.createChar(3, H_bot);
    lcd.createChar(4, C_top);
    lcd.createChar(5, C_bot);
    lcd.createChar(6, H_top);
    lcd.createChar(7, H_bot);

    // Tela de abertura
    lcd.clear();

    // "CH" à esquerda
    lcd.setCursor(0, 0);
    lcd.write(byte(0));  // C topo

    lcd.setCursor(0, 1);
    lcd.write(byte(1));  // C baixo

    lcd.setCursor(2, 0);
    lcd.write(byte(2));  // H topo

    lcd.setCursor(2, 1);
    lcd.write(byte(3));  // H baixo

    // "CH" à direita
    lcd.setCursor(13, 0);
    lcd.write(byte(4));

    lcd.setCursor(13, 1);
    lcd.write(byte(5));

    lcd.setCursor(15, 0);
    lcd.write(byte(6));

    lcd.setCursor(15, 1);
    lcd.write(byte(7));

    // Texto central
    lcd.setCursor(7, 0);
    lcd.print("CH");

    lcd.setCursor(5, 1);
    lcd.print("MONITOR");

    delay(4000);

    lcd.clear();

    // Relógio: ajusta pela data de compilação se não estiver rodando
    RTC.begin();
    if (!RTC.isrunning()) {
        RTC.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }

    carregarTriggersEEPROM();
    telaIdioma();
}


// LOOP PRINCIPAL
void loop()
{
    verificarReset();

    // Comando serial: 'L' imprime o log da EEPROM
    if (Serial.available() > 0) {
        char c = Serial.read();
        if (c == 'l' || c == 'L') {
            get_log();
        }
    }

    // --------------------------------------------------------
    // ESTADO: SELEÇÃO DE IDIOMA
    // --------------------------------------------------------
    if (estadoAtual == TELA_IDIOMA)
    {
        // LED azul durante a navegação
        setCorRGB(0, 0, 255);

        if (pressionado(JOY_SW))
        {
            idiomaAtual = (Language)idiomaSelecionado;
            estadoAtual = CONFIG_TEMP_MIN;

            exibirTelaConfiguracao();
            delay(300);
            return;
        }

        int movimento = lerJoystick();
        if (movimento == -1)  // Para cima
        {
            idiomaSelecionado--;
            if (idiomaSelecionado < 0) idiomaSelecionado = 2;
            telaIdioma();
            delay(300);
        }
        else if (movimento == 1)  // Para baixo
        {
            idiomaSelecionado++;
            if (idiomaSelecionado > 2) idiomaSelecionado = 0;
            telaIdioma();
            delay(300);
        }

        return;
    }

    // --------------------------------------------------------
    // ESTADOS: CONFIGURAÇÃO DOS LIMITES
    // --------------------------------------------------------
    if (estadoAtual >= CONFIG_TEMP_MIN && estadoAtual <= CONFIG_UMID_MAX)
    {
        // LED azul durante a configuração
        setCorRGB(0, 0, 255);

        // Botão: avança para a próxima configuração
        if (pressionado(JOY_SW))
        {
            delay(300);

            switch (estadoAtual) {
                case CONFIG_TEMP_MIN:
                    estadoAtual = CONFIG_TEMP_MAX;
                    break;
                case CONFIG_TEMP_MAX:
                    estadoAtual = CONFIG_UMID_MIN;
                    break;
                case CONFIG_UMID_MIN:
                    estadoAtual = CONFIG_UMID_MAX;
                    break;
                case CONFIG_UMID_MAX:
                    salvarTriggersEEPROM();

                    estadoAtual = TELA_PRINCIPAL;
                    lcd.clear();
                    ultimaTrocaLCD = millis() - 3000;
                    telaLCD = 0;
                    return;
                default:
                    break;
            }
            exibirTelaConfiguracao();
            return;
        }

        // Joystick: ajusta o valor da configuração atual
        int mov = lerJoystick();
        if (mov != 0)
        {
            switch (estadoAtual) {
                case CONFIG_TEMP_MIN:
                    if (idiomaAtual == INGLES) {
                        float tempF = getTemperaturaExibicao(trigger_t_min);
                        trigger_t_min = (round(tempF) + mov - 32.0) * (5.0 / 9.0);
                    } else {
                        trigger_t_min += mov;
                        trigger_t_min = round(trigger_t_min);
                    }
                    if (trigger_t_min < 0.0) trigger_t_min = 0.0;
                    if (trigger_t_min > 50.0) trigger_t_min = 50.0;
                    break;

                case CONFIG_TEMP_MAX:
                    if (idiomaAtual == INGLES) {
                        float tempF = getTemperaturaExibicao(trigger_t_max);
                        trigger_t_max = (round(tempF) + mov - 32.0) * (5.0 / 9.0);
                    } else {
                        trigger_t_max += mov;
                        trigger_t_max = round(trigger_t_max);
                    }
                    if (trigger_t_max < 0.0) trigger_t_max = 0.0;
                    if (trigger_t_max > 50.0) trigger_t_max = 50.0;
                    break;

                case CONFIG_UMID_MIN:
                    trigger_u_min += mov;
                    if (trigger_u_min < 20.0) trigger_u_min = 20.0;
                    if (trigger_u_min > 80.0) trigger_u_min = 80.0;
                    break;

                case CONFIG_UMID_MAX:
                    trigger_u_max += mov;
                    if (trigger_u_max < 20.0) trigger_u_max = 20.0;
                    if (trigger_u_max > 80.0) trigger_u_max = 80.0;
                    break;

                default:
                    break;
            }
            exibirTelaConfiguracao();
            delay(250);
        }
        return;
    }

    // ESTADO: TELA PRINCIPAL (MONITORAMENTO)

    // Leitura dos sensores e do relógio
    int luminosidade = analogRead(LDRPIN);
    float humidity = dht.readHumidity();
    float temperature = dht.readTemperature();

    DateTime now = RTC.now();
    int offsetSeconds = UTC_OFFSET * 3600;
    DateTime adjustedTime = DateTime(now.unixtime() + offsetSeconds);

    // Alarme: LED RGB e buzzer
    if (estadoAtual == TELA_PRINCIPAL)
    {
        bool foraDaFaixa = false;

        if (temperature < trigger_t_min || temperature > trigger_t_max) foraDaFaixa = true;
        if (humidity < trigger_u_min || humidity > trigger_u_max) foraDaFaixa = true;
        if (luminosidade < trigger_l_min || luminosidade > trigger_l_max) foraDaFaixa = true;

        if (foraDaFaixa) {
            // Alarme: LED vermelho
            setCorRGB(255, 0, 0);

            // Buzzer intermitente (500 ms ligado / 500 ms desligado)
            if (millis() % 1000 < 500) {
                tone(BUZZER_PIN, 1500);
            } else {
                noTone(BUZZER_PIN);
            }
        } else {
            // Tudo OK: LED verde
            setCorRGB(0, 255, 0);
            noTone(BUZZER_PIN);
        }
    }
    else
    {
        noTone(BUZZER_PIN);
    }

    if (LOG_OPTION) {
        get_log();
    }

    // Registro na EEPROM (uma vez por minuto, apenas se fora da faixa)
    if (adjustedTime.minute() != lastLoggedMinute)
    {
        lastLoggedMinute = adjustedTime.minute();

        digitalWrite(LED_BUILTIN, HIGH);
        delay(200);
        digitalWrite(LED_BUILTIN, LOW);

        if (temperature < trigger_t_min || temperature > trigger_t_max ||
            humidity < trigger_u_min || humidity > trigger_u_max)
        {
            int tempInt = (int)(temperature * 100);
            int humiInt = (int)(humidity * 100);

            EEPROM.put(currentAddress, adjustedTime.unixtime());
            EEPROM.put(currentAddress + 4, tempInt);
            EEPROM.put(currentAddress + 6, humiInt);

            getNextAddress();
        }
    }

    // Envio contínuo do horário pela Serial
    if (SERIAL_OPTION)
    {
        Serial.print(adjustedTime.day()); Serial.print("/");
        Serial.print(adjustedTime.month()); Serial.print("/");
        Serial.print(adjustedTime.year()); Serial.print(" ");
        if (adjustedTime.hour() < 10) Serial.print("0");
        Serial.print(adjustedTime.hour()); Serial.print(":");
        if (adjustedTime.minute() < 10) Serial.print("0");
        Serial.print(adjustedTime.minute()); Serial.print(":");
        if (adjustedTime.second() < 10) Serial.print("0");
        Serial.println(adjustedTime.second());
    }

    // Rotação de telas no LCD (a cada 3 segundos)
    if (millis() - ultimaTrocaLCD >= 3000)
    {
        ultimaTrocaLCD = millis();
        lcd.clear();

        if (telaLCD == 0)
        {
            // Tela 1: data e hora
            lcd.setCursor(0, 0);
            lcd.print(txtDate()); lcd.print(":");
            if (adjustedTime.day() < 10) lcd.print("0");
            lcd.print(adjustedTime.day()); lcd.print("/");
            if (adjustedTime.month() < 10) lcd.print("0");
            lcd.print(adjustedTime.month()); lcd.print("/");
            lcd.print(adjustedTime.year());

            lcd.setCursor(0, 1);
            lcd.print(txtTime()); lcd.print(":");
            if (adjustedTime.hour() < 10) lcd.print("0");
            lcd.print(adjustedTime.hour()); lcd.print(":");
            if (adjustedTime.minute() < 10) lcd.print("0");
            lcd.print(adjustedTime.minute()); lcd.print(":");
            if (adjustedTime.second() < 10) lcd.print("0");
            lcd.print(adjustedTime.second());

            telaLCD = 1;
        }
        else if (telaLCD == 1)
        {
            // Tela 2: temperatura e umidade
            lcd.setCursor(0, 0);
            lcd.print(txtTemp()); lcd.print(": ");
            lcd.print(getTemperaturaExibicao(temperature), 1);
            lcd.print(" "); lcd.print(getTempUnit());

            lcd.setCursor(0, 1);
            lcd.print(txtHumidity()); lcd.print(": ");
            lcd.print(humidity, 1); lcd.print(" %");

            telaLCD = 2;
        }
        else
        {
            // Tela 3: luminosidade
            lcd.setCursor(0, 0);
            lcd.print(txtLight());

            lcd.setCursor(0, 1);
            lcd.print(luminosidade);

            telaLCD = 0;
        }
    }
}


// FUNÇÕES AUXILIARES DA EEPROM
// Avança para o próximo registro (buffer circular)
void getNextAddress()
{
    currentAddress += recordSize;
    if (currentAddress >= endAddress) {
        currentAddress = startAddress;
    }
}

// Imprime na Serial os registros gravados na EEPROM
void get_log()
{
    Serial.println("\n--- DADOS GRAVADOS NA EEPROM ---");
    Serial.println("Timestamp\t\tTemperatura\tUmidade");

    for (int address = startAddress; address < endAddress; address += recordSize)
    {
        long timeStamp;
        int tempInt, humiInt;

        EEPROM.get(address, timeStamp);
        EEPROM.get(address + 4, tempInt);
        EEPROM.get(address + 6, humiInt);

        float temp = tempInt / 100.0;
        float hum = humiInt / 100.0;

        if (timeStamp != 0xFFFFFFFF && timeStamp != 0)
        {
            DateTime dt = DateTime(timeStamp);
            Serial.print(dt.timestamp(DateTime::TIMESTAMP_FULL));
            Serial.print("\t");
            Serial.print(temp);
            Serial.print(" C\t\t");
            Serial.print(hum);
            Serial.println(" %");
        }
    }
    Serial.println("--------------------------------\n");
}
