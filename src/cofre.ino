#include <Keypad.h>
#include <Servo.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>

#define I2C_ADDR    0x27
#define LCD_COLUMNS 16
#define LCD_LINES   2

LiquidCrystal_I2C lcd_1(I2C_ADDR, LCD_COLUMNS, LCD_LINES);

// === Servo ===
Servo trava;
int servoPin = 9;

// === LEDs e buzzer ===
int ledVerde = 11;
int ledVermelho = 13;
int buzzer = 10;

// === Teclado 4x3 ===
const byte ROWS = 4;
const byte COLS = 3;
char keys[ROWS][COLS] = {
  {'1','2','3'},
  {'4','5','6'},
  {'7','8','9'},
  {'*','0','#'}
};
byte rowPins[ROWS] = {8, 7, 6, 5};
byte colPins[COLS] = {4, 3, 2};
Keypad teclado = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// === Config senha / EEPROM endereços
const int SENHA_ADDR = 0;
const int FLAG_SILENCIO_ADDR = 10;
const int TAM_SENHA = 4;

// === Estados e variáveis ===
String senhaCorreta = "";
String senhaDigitada = "";
bool configurandoPrimeiraSenha = false;
bool modoConfig = false;
bool cofreDesbloqueado = false;

int erros = 0;
const int LIMITE_ERROS = 3;
bool bloqueado = false;

// timers
unsigned long tempoUltimaTecla = 0;  
const unsigned long TEMPO_INATIVIDADE = 30000UL;
unsigned long tempoBloqueio = 0;
const unsigned long TEMPO_BLOQUEIO = 30000UL;

// silent mode toggle (sequência # then * dentro de 1500ms)
bool aguardandoStarParaSilenciar = false;
unsigned long tempoHash = 0;
const unsigned long WINDOW_HASH_STAR = 1500UL;

// modo confirmar nova senha (digitar duas vezes)
String novaSenhaTemp = "";
bool primeiraConfirmacao = false;

// === Controle de piscada e alarme ===
unsigned long ultimoPiscaConfig = 0;
unsigned long ultimoPiscaAlarme = 0;
const unsigned long INTERVALO_PISCA_CONFIG = 450;
const unsigned long INTERVALO_ALARME = 180;

bool estadoLedConfig = false;
bool estadoLedAlarme = false;

bool alarmeAtivo = false;

// === Funções de som ===
void somTecla() { if (!isSilencioso()) tone(buzzer, 1600, 60); }

void somSucesso() { 
  if (isSilencioso()) return;
  int tons[] = {1300, 1500};
  for (int i : tons) { tone(buzzer, i, 130); delay(150); }
  noTone(buzzer);
}

void somErro() { if (!isSilencioso()) tone(buzzer, 200, 200); }

void somConfig() {
  if (isSilencioso()) return;
  int tons[] = {600, 800, 600, 1000};
  for (int i : tons) { tone(buzzer, i, 100); delay(120); }
}

// checa EEPOM flag silencioso
bool isSilencioso() {
  byte b = EEPROM.read(FLAG_SILENCIO_ADDR);
  return (b == 1);
}
void salvarSilencio(bool s) {
  EEPROM.write(FLAG_SILENCIO_ADDR, s ? 1 : 0);
}

// === EEPROM - Senha ===

void salvarSenhaEEPROM(const String &s) {
  for (int i =0; i < TAM_SENHA; i++) EEPROM.write(SENHA_ADDR + i, s[i]);
}

String lerSenhaEEPROM() {
  String s = "";
  for (int i = 0; i < TAM_SENHA; i++) s += char(EEPROM.read(SENHA_ADDR + i));
  return s;
}

bool temSenhaGravada() {
  for (int i = 0; i < TAM_SENHA; i++) {
    if (EEPROM.read(SENHA_ADDR + i) != 0xFF) return true;
  }
  return false;
}

// === Alarme não-bloqueante ===
void tocarAlarmeNaoBloqueante() {
  if (isSilencioso()) return;
  unsigned long now = millis();
  if (now - ultimoPiscaAlarme >= INTERVALO_ALARME) {
    ultimoPiscaAlarme = now;
    estadoLedAlarme = !estadoLedAlarme;
    digitalWrite(ledVermelho, estadoLedAlarme);

    if (estadoLedAlarme) {
      tone(buzzer, 1000, 150);
    } else {
      tone(buzzer, 1300, 150);
    }
  }
}

// === Utilitários ===
void mostrarMsg(const String &msg, bool sucesso = false, const String &returnPrompt = "Digite a senha:")  {
  lcd_1.clear();
  lcd_1.print(msg);
  if (sucesso) somSucesso(); else somErro();
  delay(1000);
  lcd_1.clear();
  lcd_1.print(returnPrompt);
}

// === Ações do cofre ===
void abrirCofre() {
  cofreDesbloqueado = true;
  erros = 0;
  alarmeAtivo = false;
  bloqueado = false;
  noTone(buzzer);

  lcd_1.clear();
  lcd_1.print("Acesso Liberado");
  digitalWrite(ledVerde, HIGH);
  trava.write(0);
  somSucesso();
  delay(2000);

  lcd_1.clear();
  lcd_1.print("Press. '#' p/ fechar");
  lcd_1.setCursor(0,1);
  lcd_1.print("* p/ configurar");

  while (true) {
    char t = teclado.getKey();
    
    if (alarmeAtivo) tocarAlarmeNaoBloqueante();

    if (t == '#') {
      // === Animação de fechamento
        lcd_1.clear();
        lcd_1.print("Fechando cofre");
        
        for (int i = 0; i < 3; i++) {
          lcd_1.print(".");
          digitalWrite(ledVerde, HIGH);
          if (!isSilencioso()) {
            tone(buzzer, 900, 100);
            delay(300);
            digitalWrite(ledVerde, LOW);
            delay(200);
          }
        }
        
      trava.write(90);
      digitalWrite(ledVerde, LOW);
      cofreDesbloqueado = false;
      
      delay(500);
      mostrarMsg("Cofre fechado", true);
      return;
    }

    if (t == '*') {
      modoConfig = true;
      somConfig();
      novaSenhaTemp = "";
      primeiraConfirmacao = false;
      lcd_1.clear();
      lcd_1.print("Nova senha (1/2):");
      digitalWrite(ledVermelho, HIGH);
      digitalWrite(ledVerde, HIGH);
      return;
    }
  }

  delay(50);
}

void erroSenha() {
  erros++;

  digitalWrite(ledVermelho, HIGH);
  mostrarMsg("Senha incorreta", false);
  delay(700);
  digitalWrite(ledVermelho, LOW);
  senhaDigitada = "";

  if (erros >= LIMITE_ERROS) {
    alarmeAtivo = true;
    bloqueado = true;
    tempoBloqueio = millis();
    lcd_1.clear();
    lcd_1.print("ALARME ATIVADO!");
    lcd_1.setCursor(0,1);
    lcd_1.print("Bloqueado 30s");
  }
}

// === Inatividade ===
void verificarInatividade() {
  if (senhaDigitada.length() > 0 && (millis() - tempoUltimaTecla > TEMPO_INATIVIDADE)) {
    senhaDigitada = "";
    mostrarMsg("Tempo esgotado!", false);
  }
}

// === Setup ===
void setup() {
  lcd_1.begin(16, 2);
  lcd_1.setBacklight(1);
  trava.attach(servoPin);
  delay(200);
  trava.write(90);
  delay(300);
  
  pinMode(ledVerde, OUTPUT);
  pinMode(ledVermelho, OUTPUT);
  pinMode(buzzer, OUTPUT);

  // inicializa flags
  if (!temSenhaGravada()) {
    configurandoPrimeiraSenha = true;
    lcd_1.clear();
    lcd_1.print("Cadastre senha:");
    lcd_1.setCursor(0,1);
    lcd_1.print("(4 digitos) '#' confirma");
    senhaDigitada = "";
  } else {
    senhaCorreta = lerSenhaEEPROM();
    lcd_1.clear();
    lcd_1.print("Digite a senha:");
  }
}

// === Loop principal ===
void loop() {
  char tecla = teclado.getKey();
  unsigned long now = millis();

  if (alarmeAtivo) tocarAlarmeNaoBloqueante(); 

  if (bloqueado && (millis() - tempoBloqueio >= TEMPO_BLOQUEIO)) {
    bloqueado = false;
    alarmeAtivo = false;
    erros = 0;
    noTone(buzzer);
    lcd_1.clear();
    lcd_1.print("Digite a senha:");
    digitalWrite(ledVermelho, LOW);
  }

  verificarInatividade();

  if (!tecla || bloqueado) return;

  tempoUltimaTecla = millis();
  somTecla();

  // === Configuração inicial ===
  if (configurandoPrimeiraSenha) {
    if (isDigit(tecla)) {
      if (senhaDigitada.length() < TAM_SENHA) {
        senhaDigitada += tecla;
        lcd_1.setCursor(0,1);
        for (int i=0; i<senhaDigitada.length(); i++) lcd_1.print('*');
        if (senhaDigitada.length() == 1) {
          lcd_1.clear();
          lcd_1.print("Cadastre senha:");
          lcd_1.setCursor(0,1);
          lcd_1.print('*');
        }
      }
      return;
    }
    if (tecla == '#') {
      if (senhaDigitada.length() == TAM_SENHA) {
        salvarSenhaEEPROM(senhaDigitada);
        senhaCorreta = senhaDigitada;
        configurandoPrimeiraSenha = false;
        mostrarMsg("Senha salva!", true);
        senhaDigitada = "";
      } else {
        mostrarMsg("Use 4 digitos!", false, "Cadastre senha:");
        senhaDigitada = "";
      }
    }
    return;
  }

  // === Modo configuração ===
  if (modoConfig) {
    if(isDigit(tecla)) {
      if (senhaDigitada.length() < TAM_SENHA) {
        senhaDigitada += tecla;
        lcd_1.setCursor(0,1);
        for (int i = 0; i <senhaDigitada.length(); i++) lcd_1.print("*");
      }
      return;
    }
    if (tecla == '#') {
      if (!primeiraConfirmacao) {
        if (senhaDigitada.length() == TAM_SENHA) {
          novaSenhaTemp = senhaDigitada;
          senhaDigitada = "";
          primeiraConfirmacao = true;
          lcd_1.clear();
          lcd_1.print("Confirme (2/2):");
        } else {
          mostrarMsg("Use 4 digitos!", false, "Nova senha (1/2):");
          senhaDigitada = "";
        }
      } else {
        if (senhaDigitada.length() == TAM_SENHA && senhaDigitada == novaSenhaTemp) {
          salvarSenhaEEPROM(senhaDigitada);
          senhaCorreta = senhaDigitada;
          modoConfig = false;
          primeiraConfirmacao = false;
          novaSenhaTemp = "";
          senhaDigitada = "";
          lcd_1.clear();
          lcd_1.print("Senha alterada!");
          somSucesso();

          for (int i = 0; i < 4; i++) {
            digitalWrite(ledVerde, HIGH);
            digitalWrite(ledVermelho, HIGH);
            delay(200);
            digitalWrite(ledVerde, LOW);
            digitalWrite(ledVermelho, LOW);
            delay(200);
          }

          // === Animação de fechamento
          lcd_1.setCursor(0,1);
          lcd_1.print("Fechando cofre");

          for (int i = 0; i < 3; i++) {
            lcd_1.print(".");
            digitalWrite(ledVerde, HIGH);
            if (!isSilencioso()) tone(buzzer, 900, 100);
            delay(300);
            digitalWrite(ledVerde, LOW);
            delay(200);
          }

          trava.write(90);
          digitalWrite(ledVerde, LOW);
          cofreDesbloqueado = false;


          delay(500);
          lcd_1.clear();
          lcd_1.print("Cofre fechado");
          delay(1000);
          lcd_1.clear();
          lcd_1.print("Digite a senha:");
        } else {
          mostrarMsg("Não confere!", false, "Nova senha (1/2):");
          primeiraConfirmacao = false;
          novaSenhaTemp = "";
          senhaDigitada = "";
        }
      }
    } else if (tecla == '*') {
      primeiraConfirmacao = false;
      novaSenhaTemp = "";
      senhaDigitada = "";
      modoConfig = false;
      mostrarMsg("Cancelando...", false);
      digitalWrite(ledVermelho, LOW);
      digitalWrite(ledVerde, LOW);
    }
    return;
  } else {
  }

  // === Modo Normal ===
  if (isDigit(tecla)) {
    if (senhaDigitada.length() < TAM_SENHA) {
      senhaDigitada += tecla;
      lcd_1.setCursor(0,1);
      for (int i = 0; i < senhaDigitada.length(); i++) lcd_1.print('*');
    }
    return;
  }

  if (tecla == '#') {
    // confirmar senha
    if (senhaDigitada.length() == TAM_SENHA) {
      if (senhaDigitada == senhaCorreta) {
        abrirCofre();
        senhaDigitada = "";
        return;
      } else {
        erroSenha();
        return;
      }
    } else {
      mostrarMsg("Senha = 4 digitos!");
      senhaDigitada = "";
      return;
    }
  }

  if (tecla == '*') {
  // === MENU GERAL ===
    if (!cofreDesbloqueado && !modoConfig) {
      lcd_1.clear();
      lcd_1.print("Configuracoes:");
      lcd_1.setCursor(0, 1);
      lcd_1.print("1-Som  2-Info");

      while (true) {
        char op = teclado.getKey();
        if (!op) continue;

        if (op == '1') {
          bool novo = !isSilencioso();
          salvarSilencio(novo);
          lcd_1.clear();
          lcd_1.print(novo ? "Modo silencioso" : "Som ativado");
          if(!isSilencioso()) somSucesso();
          delay(1500);
          lcd_1.clear();
          lcd_1.print("Configuracoes:");
          lcd_1.setCursor(0, 1);
          lcd_1.print("1-Som  2-Info");
        }

        else if (op == '2') {
          lcd_1.clear();
          lcd_1.print("Som: ");
          lcd_1.print(isSilencioso() ? "OFF" : "ON");
          lcd_1.setCursor(0, 1);
          lcd_1.print("Tentativas: ");
          lcd_1.print(erros);
          delay(2500);
          lcd_1.clear();
          lcd_1.print("Configuracoes:");
          lcd_1.setCursor(0, 1);
          lcd_1.print("1-Som  2-Info");
        }

        else if (op == '#') {
          lcd_1.clear();
          lcd_1.print("Saindo...");
          delay(1000);
          lcd_1.clear();
          lcd_1.print("Digite a senha:");
          break;
        }
      }
      return;
    }
  }
}