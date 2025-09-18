// ====================================================================
// IMPORTAÇÃO DAS BIBLIOTECAS
// ====================================================================
#include <Adafruit_Fingerprint.h>
#include <TFT_eSPI.h>
#include <Keypad.h>
#include <ESP32Servo.h>

// ====================================================================
// CONFIGURAÇÕES DO SENSOR DE BIOMETRIA
// ====================================================================
#define FINGERPRINT_RX_PIN 36
#define FINGERPRINT_TX_PIN 27

// ====================================================================
// CONFIGURAÇÕES DO BUZZER E LEDs
// ====================================================================
#define BUZZER_PIN 21
#define LED_SUCCESS_PIN 22
#define LED_FAIL_PIN 17

// ====================================================================
// CONFIGURAÇÕES DO SERVO
// ====================================================================
#define SERVO_PIN 26

// ====================================================================
// CONFIGURAÇÕES DO TECLADO
// ====================================================================
const uint8_t ROWS = 4;
const uint8_t COLS = 3;
char keys[ROWS][COLS] = {
    {'1', '2', '3'},
    {'4', '5', '6'},
    {'7', '8', '9'},
    {'*', '0', '#'}
};
uint8_t colPins[COLS] = {32, 33, 25};
uint8_t rowPins[ROWS] = {12, 13, 15, 2};
// ====================================================================
// CONFIGURAÇÃO DO BOTÃO DE ADMINISTRAÇÃO E SENHA
// ====================================================================
#define ADMIN_BUTTON_PIN 35

// Coloque sua senha de administrador aqui
const char admin_password[] = "1";
String current_password = "";

// ====================================================================
// ESTADOS DO SISTEMA
// ====================================================================
enum SystemState {
    STATE_VERIFYING,
    STATE_ADMIN_AUTH,
    STATE_ADMIN_MENU,
    STATE_ENROLLING,
    STATE_DELETING_BY_FINGER,
    STATE_DELETING_BY_ID,
    STATE_DELETING_ALL
};
SystemState currentState = STATE_VERIFYING;

// ====================================================================
// OBJETOS DO PROGRAMA
// ====================================================================
HardwareSerial mySerial(2);
Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);
TFT_eSPI tft = TFT_eSPI();

// Objeto para controlar o servo
Servo tranca;
uint8_t id_to_enroll = 0;

// ====================================================================
// CONFIGURAÇÕES DE PWM PARA BUZZER E SERVO
// ====================================================================
const int BUZZER_CHANNEL = 0;
const int BUZZER_FREQ = 2000;
const int BUZZER_RES = 10;

// ====================================================================
// PROTÓTIPOS DE FUNÇÃO
// ====================================================================

void setup();
void loop();
void playSuccessSoundAndLight(uint8_t fingerID);
void playFailSoundAndLight();
uint8_t getFingerprintID();
uint8_t getFingerprintEnroll(uint8_t id);
void startAdminMode();
void handleAdminAuth();
void handleAdminMenu();
void deleteFingerprintBySensor();
void deleteFingerprintByID();
void deleteAllFingerprints();
void returnToVerifyMode();
void showStatusScreen();
void showAdminAuthScreen();
void showAdminMenuScreen();
void showEnrollScreen(int step);
uint8_t getNextAvailableID();
bool checkAdminButton();
uint8_t readNumberFromKeypad();
void tone(int pin, int freq, int duration);


/*
  @brief Função de inicialização do programa.
  
  Configura a comunicação serial, inicializa a tela TFT,
  o buzzer, os LEDs, o servo motor e o sensor de impressão digital.
  Verifica a conexão com o sensor e, se bem-sucedida, exibe a tela de status.
*/
void setup() {
    Serial.begin(9600);
    while (!Serial);
    delay(100);
    Serial.println("\n\nTTGO T-Display with Fingerprint Sensor");

    tft.init();
    tft.setRotation(1);
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString("Iniciando...", 0, 0, 2);
    // Configuração do canal de PWM para o BUZZER (Timer 0, High-speed)
    ledcSetup(BUZZER_CHANNEL, BUZZER_FREQ, BUZZER_RES);
    ledcAttachPin(BUZZER_PIN, BUZZER_CHANNEL);
    ledcWrite(BUZZER_CHANNEL, 0); // Silencia o buzzer no boot

    pinMode(LED_SUCCESS_PIN, OUTPUT);
    pinMode(LED_FAIL_PIN, OUTPUT);
    digitalWrite(LED_SUCCESS_PIN, LOW);
    digitalWrite(LED_FAIL_PIN, LOW);
    // Configuração do servo em um timer LOW_SPEED separado para evitar conflitos
    ESP32PWM::allocateTimer(2);
    tranca.setPeriodHertz(50);
    tranca.attach(SERVO_PIN, 500, 2400);
    tranca.write(180); // Posição inicial (fechada)
    tranca.detach(); // Desconecta o servo para evitar vibrações durante funcionamento

    pinMode(ADMIN_BUTTON_PIN, INPUT_PULLUP);
    mySerial.begin(57600, SERIAL_8N1, FINGERPRINT_RX_PIN, FINGERPRINT_TX_PIN);
    delay(5);

    if (finger.verifyPassword()) {
        tft.fillScreen(TFT_BLACK);
        tft.drawString("Sensor OK!", 0, 0, 2);
        delay(1000);
    } else {
        tft.fillScreen(TFT_BLACK);
        tft.drawString("Sensor NAO encontrado!", 0, 0, 2);
        while (1) { delay(1); }
    }
    
    showStatusScreen();
}

/*
  @brief Função principal do programa.
  
  Executa um laço infinito que gerencia o estado do sistema
  (máquina de estados). De acordo com o estado atual (ex: verificando,
  autenticando admin, menu admin), a função chama a sub-rotina
  correspondente.
*/
void loop() {
    
    switch (currentState) {
        case STATE_VERIFYING:
            if (checkAdminButton()) {
                startAdminMode();
            } else {
                getFingerprintID();
            }
            break;
        case STATE_ADMIN_AUTH:
            handleAdminAuth();
            break;
        case STATE_ADMIN_MENU:
            handleAdminMenu();
            break;
        case STATE_ENROLLING:
            getFingerprintEnroll(id_to_enroll);
            returnToVerifyMode();
            break;
        case STATE_DELETING_BY_FINGER:
            deleteFingerprintBySensor();
            break;
        case STATE_DELETING_BY_ID:
            deleteFingerprintByID();
            break;
        case STATE_DELETING_ALL:
            deleteAllFingerprints();
            break;
    }
    delay(50);
}

/*
  @brief Aciona LEDs e buzzer para indicar sucesso na autenticação.
  
  Exibe uma mensagem de "ACESSO LIBERADO" na tela com o ID da digital
  e emite dois bipes. Em seguida, aciona o servo motor para abrir e
  fechar a tranca e, por fim, retorna à tela de status.
*/
void playSuccessSoundAndLight(uint8_t fingerID) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("ACESSO LIBERADO", 0, 0, 4);
    
    tft.setTextFont(4);
    String idString = "ID: " + String(fingerID);
    int16_t x = tft.width() / 2 - tft.textWidth(idString) / 2;
    tft.drawString(idString, x, 40);
    tone(BUZZER_PIN, 1500, 100);
    delay(50);
    tone(BUZZER_PIN, 2000, 100);
    
    digitalWrite(LED_SUCCESS_PIN, HIGH);
    digitalWrite(LED_FAIL_PIN, LOW);
    // Ligar o servo para o movimento de abrir
    if (!tranca.attached()) {
      tranca.attach(SERVO_PIN, 500, 2400);
    }
    
    // Mover o servo para abrir a tranca
    tranca.write(45);
    Serial.println("Tranca aberta.");

    delay(3000); 

    // Mover o servo para fechar a tranca
    tranca.write(180); 
    Serial.println("Tranca fechada.");

    delay(500);
    // Desconectar o servo novamente como na inicialização para evitar vibrações
    tranca.detach();
    
    digitalWrite(LED_SUCCESS_PIN, LOW);
    showStatusScreen();
}

/*
  @brief Aciona LEDs e buzzer para indicar falha na autenticação.
  
  Exibe uma mensagem de "ACESSO NEGADO" na tela, emite um bipe de
  alerta, acende o LED de falha e, após um breve atraso,
  desliga-o e retorna à tela de status.
*/
void playFailSoundAndLight() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextFont(4);
    tft.drawString("ACESSO NEGADO", 0, 0);
    tone(BUZZER_PIN, 500, 300);
    digitalWrite(LED_FAIL_PIN, HIGH);
    digitalWrite(LED_SUCCESS_PIN, LOW);
    delay(2000);
    digitalWrite(LED_FAIL_PIN, LOW);
    showStatusScreen();
}

/*
  @brief Emite um bipe de alerta específico para falha de senha de administrador.
*/
void playAdminFailSound() {
    tone(BUZZER_PIN, 300, 500);
}

/*
  @brief Exibe a tela de status principal do sistema.
  
  Mostra a mensagem "Aguardando dedo..." e a contagem total de
  digitais cadastradas no sensor.
*/
void showStatusScreen() {
    finger.getTemplateCount();
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextFont(2);
    tft.drawString("Aguardando dedo...", 0, 0);
    tft.setCursor(0, 30);
    tft.print("Digitais cadastradas: ");
    tft.println(finger.templateCount);
}

/*
  @brief Exibe a tela de autenticação para o modo administrador.
  
  Solicita que o usuário digite a senha de administrador no teclado.
*/
void showAdminAuthScreen() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextFont(4);
    tft.drawString("MODO ADMIN", 0, 0);
    tft.setTextFont(2);
    tft.setCursor(0, 40);
    tft.print("Senha: ");
}

/*
  @brief Exibe o menu do modo administrador.
  
  Apresenta as opções para cadastrar, remover digitais (por biometria
  ou ID) ou remover todas.
*/
void showAdminMenuScreen() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextFont(4);
    tft.drawString("MENU ADMIN", 0, 0);
    tft.setTextFont(2);
    tft.setCursor(0, 40);
    tft.print("1: Cadastrar Digital");
    tft.setCursor(0, 60);
    tft.print("2: Remover por Biometria");
    tft.setCursor(0, 80);
    tft.print("3: Remover por ID");
    tft.setCursor(0, 100);
    tft.print("4: Remover TUDO");
    tft.setCursor(0, 120);
    tft.print("0: Voltar para tela inicial");
}

/*
  @brief Exibe as diferentes etapas do processo de cadastro de digital.
  
  As mensagens na tela são dinâmicas e mudam de acordo com o
  passo atual do cadastro.
*/
void showEnrollScreen(int step) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.setTextFont(4);
    tft.drawString("CADASTRO", 0, 0);
    tft.setTextFont(2);
    tft.setCursor(0, 40);

    if (step == 0) {
        tft.print("ID sugerido: ");
        tft.print(id_to_enroll);
        tft.setCursor(0, 60);
        tft.print("Confirme com # ou digite um novo ID");
        tft.setCursor(0, 80);
        tft.print("Pressione * para cancelar.");
    } else if (step == 1) {
        tft.print("Coloque o dedo.");
        tft.setCursor(0, 60);
        tft.print("ID: "); tft.print(id_to_enroll);
        tft.setCursor(0, 80);
        tft.print("Pressione * para cancelar.");
    } else if (step == 2) {
        tft.print("Tire o dedo.");
    } else if (step == 3) {
        tft.print("Coloque o mesmo dedo novamente.");
        tft.setCursor(0, 60);
        tft.print("ID: "); tft.print(id_to_enroll);
    } else if (step == 4) {
        tft.print("Salvando...");
    } else if (step == 5) {
        tft.print("Cadastro concluido!");
        delay(2000);
    } else if (step == 6) {
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.print("Falha no cadastro.");
        delay(2000);
    }
}

/*
  @brief Encontra o próximo ID disponível para cadastro no sensor.
  
  Percorre o banco de dados do sensor, a partir do ID 1, e retorna
  o primeiro ID que não está em uso.
*/
uint8_t getNextAvailableID() {
    uint8_t p = -1;
    finger.getTemplateCount();
    if (finger.templateCount > 0) {
        for (uint8_t i = 1; i < finger.capacity; i++) {
            p = finger.loadModel(i);
            if (p != FINGERPRINT_OK) {
                return i;
            }
        }
    }
    return 1;
}

/*
  @brief Verifica se o botão de administração foi pressionado.
  
  Retorna `true` se o botão (configurado como INPUT_PULLUP) estiver
  pressionado (nível LOW), caso contrário, retorna `false`.
*/
bool checkAdminButton() {
    return digitalRead(ADMIN_BUTTON_PIN) == LOW;
}

/*
  @brief Inicia o modo de administração do sistema.
  
  Define o estado do sistema para `STATE_ADMIN_AUTH` para iniciar a
  solicitação de senha e exibe a tela de autenticação.
*/
void startAdminMode() {
    currentState = STATE_ADMIN_AUTH;
    current_password = "";
    showAdminAuthScreen();
    Serial.println("Entrando no modo de administracao.");
}

/*
  @brief Gerencia a entrada da senha no teclado para o modo de administração.
  
  Lê os caracteres digitados. Ao pressionar '#', verifica a senha.
  Se correta, muda o estado para `STATE_ADMIN_MENU`.
  Se incorreta, reproduz um som de falha e retorna ao modo de verificação.
*/
void handleAdminAuth() {
    char key = keypad.getKey();
    if (key != NO_KEY) {
        tft.print(key);
        if (key == '#') {
            if (current_password == admin_password) {
                Serial.println("Senha correta!");
                tft.fillScreen(TFT_BLACK);
                tft.setTextColor(TFT_GREEN, TFT_BLACK);
                tft.setTextFont(4);
                tft.drawString("SENHA CORRETA", 0, 0);
                delay(1500);
                currentState = STATE_ADMIN_MENU;
                showAdminMenuScreen();
            } else {
                Serial.println("Senha incorreta!");
                tft.fillScreen(TFT_BLACK);
                tft.setTextColor(TFT_RED, TFT_BLACK);
                tft.setTextFont(4);
                tft.drawString("SENHA INCORRETA", 0, 0);
                playAdminFailSound();
                delay(1500);
                returnToVerifyMode();
            }
        } else if (key != '*') {
            current_password += key;
        }
    }
}

/*
  @brief Gerencia as opções do menu de administração.
  
  Lê a tecla pressionada no teclado e, com base na opção
  selecionada (1, 2, 3, 4 ou 0), muda o estado do sistema
  para a função correspondente (cadastrar, remover, etc.).
*/
void handleAdminMenu() {
    char key = keypad.getKey();
    if (key != NO_KEY) {
        if (key == '1') {
            currentState = STATE_ENROLLING;
            id_to_enroll = getNextAvailableID();
            showEnrollScreen(0);
        } else if (key == '2') {
            currentState = STATE_DELETING_BY_FINGER;
            deleteFingerprintBySensor();
        } else if (key == '3') {
            currentState = STATE_DELETING_BY_ID;
            deleteFingerprintByID();
        } else if (key == '4') {
            currentState = STATE_DELETING_ALL;
            deleteAllFingerprints();
        } else if (key == '0') {
            returnToVerifyMode();
        }
    }
}

/*
  @brief Remove uma digital do sensor utilizando a leitura do próprio dedo.
  
  Solicita que o usuário coloque o dedo, captura a imagem,
  busca o ID correspondente e, se encontrado, executa a
  função de remoção. Exibe mensagens na tela para cada etapa.
*/
void deleteFingerprintBySensor() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextFont(4);
    tft.drawString("REMOVER DIGITAL", 0, 0);
    tft.setTextFont(2);
    tft.setCursor(0, 40);
    tft.print("Coloque o dedo para remover...");
    tft.setCursor(0, 60);
    tft.print("Pressione * para cancelar.");
    
    uint8_t p = -1;
    while (p != FINGERPRINT_OK) {
        p = finger.getImage();
        
        char key = keypad.getKey();
        if (key == '*') {
            tft.fillScreen(TFT_BLACK);
            tft.drawString("Operacao cancelada.", 0, 0, 2);
            delay(1500);
            currentState = STATE_ADMIN_MENU;
            showAdminMenuScreen();
            return;
        }
        
        delay(100);
    }
    
    p = finger.image2Tz();
    if (p != FINGERPRINT_OK) {
        tft.fillScreen(TFT_BLACK);
        tft.drawString("Erro na imagem", 0, 0, 2);
        delay(2000);
        currentState = STATE_ADMIN_MENU;
        showAdminMenuScreen();
        return;
    }
    
    p = finger.fingerSearch();
    if (p == FINGERPRINT_OK) {
        uint8_t id_found = finger.fingerID;
        tft.fillScreen(TFT_BLACK);
        tft.setTextFont(2);
        tft.drawString("Removendo ID " + String(id_found) + "...", 0, 0);
        
        uint8_t delete_status = finger.deleteModel(id_found);
        if (delete_status == FINGERPRINT_OK) {
            tft.drawString("Digital removida!", 0, 20);
        } else {
            tft.drawString("Erro ao remover digital.", 0, 20);
        }
    } else {
        tft.fillScreen(TFT_BLACK);
        tft.setTextFont(2);
        tft.drawString("Digital nao encontrada!", 0, 0);
    }
    
    delay(2000);
    currentState = STATE_ADMIN_MENU;
    showAdminMenuScreen();
}

/*
  @brief Remove uma digital do sensor usando seu ID numérico.
  
  Solicita ao usuário que digite o ID no teclado,
  lê o valor e executa a função de remoção.
*/
void deleteFingerprintByID() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextFont(4);
    tft.drawString("REMOVER POR ID", 0, 0);
    tft.setTextFont(2);
    tft.setCursor(0, 40);
    tft.print("Digite o ID a remover: ");
    tft.setCursor(0, 60);
    tft.print("Pressione * para cancelar.");

    int id_to_delete = readNumberFromKeypad();
    if (id_to_delete == 0) { 
        tft.fillScreen(TFT_BLACK);
        tft.drawString("Operacao cancelada.", 0, 0, 2);
        delay(1500);
        currentState = STATE_ADMIN_MENU;
        showAdminMenuScreen();
        return;
    }

    tft.fillScreen(TFT_BLACK);
    tft.setTextFont(2);
    tft.drawString("Removendo ID " + String(id_to_delete) + "...", 0, 0);
    uint8_t delete_status = finger.deleteModel(id_to_delete);
    
    if (delete_status == FINGERPRINT_OK) {
        tft.drawString("Digital removida!", 0, 20);
    } else {
        tft.drawString("ID nao encontrado ou erro.", 0, 20);
    }

    delay(2000);
    currentState = STATE_ADMIN_MENU;
    showAdminMenuScreen();
}

/*
  @brief Remove todas as digitais do banco de dados do sensor.
  
  Pede confirmação do usuário via teclado. Se confirmada,
  chama a função `emptyDatabase()` do sensor para apagar
  todos os registros.
*/
void deleteAllFingerprints() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.setTextFont(4);
    tft.drawString("REMOVER TUDO", 0, 0);
    tft.setTextFont(2);
    tft.setCursor(0, 40);
    tft.print("Confirmar? Pressione #");
    tft.setCursor(0, 60);
    tft.print("Pressione * para cancelar.");
    
    char key = NO_KEY;
    while (key != '#' && key != '*') {
        key = keypad.getKey();
        delay(50);
    }
    
    if (key == '*') {
        tft.fillScreen(TFT_BLACK);
        tft.drawString("Operacao cancelada.", 0, 0, 2);
        delay(1500);
        currentState = STATE_ADMIN_MENU;
        showAdminMenuScreen();
        return;
    }
    
    tft.fillScreen(TFT_BLACK);
    tft.setTextFont(2);
    tft.drawString("Removendo todas...", 0, 0);
    
    uint8_t p = finger.emptyDatabase();
    
    if (p == FINGERPRINT_OK) {
        tft.drawString("Todas removidas!", 0, 20);
    } else {
        tft.drawString("Erro ao remover tudo.", 0, 20);
    }
    
    delay(2000);
    currentState = STATE_ADMIN_MENU;
    showAdminMenuScreen();
}

/*
  @brief Retorna o sistema ao modo de verificação padrão.
  
  Redefine o estado do sistema e a senha atual para a tela de status
  inicial, onde o sistema aguarda uma digital para verificação.
*/
void returnToVerifyMode() {
    currentState = STATE_VERIFYING;
    current_password = "";
    showStatusScreen();
}

/*
  @brief Tenta encontrar uma digital correspondente no banco de dados.
  
  Captura uma imagem do sensor, converte-a para um modelo e
  busca por uma correspondência. Se encontra uma, aciona
  `playSuccessSoundAndLight()`. Se não, `playFailSoundAndLight()`.
*/
uint8_t getFingerprintID() {
    uint8_t p = finger.getImage();
    if (p == FINGERPRINT_OK) {
        p = finger.image2Tz();
        if (p == FINGERPRINT_OK) {
            p = finger.fingerSearch();
            if (p == FINGERPRINT_OK) {
                Serial.println("Encontrada uma digital correspondente!");
                playSuccessSoundAndLight(finger.fingerID);
            } else if (p == FINGERPRINT_NOTFOUND) {
                Serial.println("Nao foi encontrada uma correspondencia");
                playFailSoundAndLight();
            }
        }
    }
    return p;
}

/*
  @brief Guia o usuário pelo processo de cadastro de uma nova digital.
  
  A função interage com a tela TFT para orientar o usuário a colocar
  e retirar o dedo duas vezes. Em seguida, cria um modelo da digital
  e o salva no sensor com o ID fornecido.
*/
uint8_t getFingerprintEnroll(uint8_t id) {
    int p = -1;
    Serial.print("Aguardando dedo para cadastrar ID #");
    Serial.println(id);
    showEnrollScreen(1);
    while (p != FINGERPRINT_OK) {
        p = finger.getImage();
        
        char key = keypad.getKey();
        if (key == '*') {
            tft.fillScreen(TFT_BLACK);
            tft.drawString("Operacao cancelada.", 0, 0, 2);
            delay(1500);
            currentState = STATE_ADMIN_MENU;
            showAdminMenuScreen();
            return 0;
        }
        
        delay(100);
    }
    p = finger.image2Tz(1);
    
    Serial.println("Remova o dedo");
    showEnrollScreen(2);
    delay(2000);
    
    p = 0;
    while (p != FINGERPRINT_NOFINGER) {
        p = finger.getImage();
        char key = keypad.getKey();
        if (key == '*') {
            tft.fillScreen(TFT_BLACK);
            tft.drawString("Operacao cancelada.", 0, 0, 2);
            delay(1500);
            currentState = STATE_ADMIN_MENU;
            showAdminMenuScreen();
            return 0;
        }
    }
    
    Serial.println("Coloque o mesmo dedo novamente");
    showEnrollScreen(3);
    p = -1;
    while (p != FINGERPRINT_OK) {
        p = finger.getImage();
        char key = keypad.getKey();
        if (key == '*') {
            tft.fillScreen(TFT_BLACK);
            tft.drawString("Operacao cancelada.", 0, 0, 2);
            delay(1500);
            currentState = STATE_ADMIN_MENU;
            showAdminMenuScreen();
            return 0;
        }
        delay(100);
    }
    p = finger.image2Tz(2);
    Serial.print("Criando modelo para ID #");
    Serial.println(id);
    showEnrollScreen(4);
    p = finger.createModel();
    if (p == FINGERPRINT_OK) {
        Serial.println("Digitais correspondem!");
        p = finger.storeModel(id);
        if (p == FINGERPRINT_OK) {
            Serial.println("Salvo!");
            showEnrollScreen(5);
        } else {
            showEnrollScreen(6);
        }
    } else {
        Serial.println("Digitais nao correspondem");
        showEnrollScreen(6);
    }
    
    return p;
}

/*
  @brief Lê um número digitado no teclado.
  
  Aguarda a entrada de caracteres numéricos (0-9) e os concatena.
  A leitura termina quando a tecla '#' é pressionada, e o valor
  resultante é convertido para um inteiro e retornado. A tecla '*'
  cancela a operação, retornando 0.
*/
uint8_t readNumberFromKeypad() {
    String numString = "";
    char key = NO_KEY;

    while (true) {
        key = keypad.getKey();
        if (key != NO_KEY) {
            if (key == '*') {
                return 0;
            } else if (key >= '0' && key <= '9') {
                numString += key;
                tft.print(key);
            } else if (key == '#') {
                if (numString.length() > 0) {
                    return numString.toInt();
                }
            }
        }
        delay(50);
    }
}

/*
  @brief Gera um som no buzzer usando PWM do ESP32.
  
  @param pin O pino do buzzer (não é usado, o canal de PWM já está
             associado ao pino do buzzer, mas mantido na assinatura).
  @param freq A frequência do som em Hz.
  @param duration A duração do som em milissegundos. Se 0, o som é
                  tocado indefinidamente até que a função seja chamada
                  com freq 0.
*/
void tone(int pin, int freq, int duration) {
    if (freq > 0) {
        ledcWriteTone(BUZZER_CHANNEL, freq);
    } else {
        ledcWrite(BUZZER_CHANNEL, 0);
    }
    if (duration > 0) {
        delay(duration);
        ledcWrite(BUZZER_CHANNEL, 0);
    }
}