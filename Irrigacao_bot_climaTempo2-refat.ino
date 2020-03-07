/********************************************************
 *J. A. Lisboa 14/01/2019                              *
 *Controle de irrigacao usando chatbot com Telegram     *
 *Programacao via ota                                   *
 ********************************************************/
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <EEPROM.h>
#include <time.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

// Dados da conexão WiFi
char ssid[] = "mel";     // Nome da rede
char password[] = "morandonaselva"; // Senha
const char* host = "apiadvisor.climatempo.com.br";
String urlTemp = "/api/v1/weather/locale/5448/current?token=3e3eee2fe995c0c3aa17b5e4dce813cf";
String urlRain = "/api/v1/history/locale/5448?token=3e3eee2fe995c0c3aa17b5e4dce813cf&from=2019-11-18";

// Initialize Telegram BOT
//#define BOTtoken "578304230:AAH5XorX8xN8eFpcba4HFAqQzmol_xGPE4Q"  // IDToken para PY3MH
//#define BOTtoken "697666653:AAHHYLcaJ5AOfKYPFWMzhoWV_qYU8-YkiWE"  // IDToken para Dosador
//#define BOTtoken "1012551849:AAGqFdXE_7tO-3sHRXWUHnHjrhvSE0Qi-1g"  // IDToken para IrrigacaoBot (Cleo)
#define BOTtoken "1012551849:AAEhO6jQ-xf7bekfu7gdc0dt7MsMhrZG_3E"  //novo token (Cleo)

WiFiClientSecure client;
WiFiClient clientHttp;

UniversalTelegramBot bot(BOTtoken, client);

int Bot_mtbs = 1000; //Tempo medio entre escaneios de mensagem
long Bot_lasttime;   //Instante do ultimo escaneio
int MotorStatus,SistemStatus,acionadoPeloUsuario=0;
bool Start = false;
int N, timerN, timerDuracao, contextoSchedule;
long int H[6],M[6],D[6],L[6];
long int TM;
const int MOTOR = 16;             //Relé que aciona o motor
int timezone = -3;                //Fuso horario
int dst=0;                        //Horario de verão      
int hora,minuto;
int horaArray[10], minutoArray[10];
String contexto = "";

//Faz o tratamento das mensagens
void handleNewMessages(int numNewMessages){
    Serial.println("Numero de novas mensagens: "+String(numNewMessages));
    for (int i=0; i<numNewMessages; i++) {
        String chat_id = String(bot.messages[i].chat_id);             //Identifica quem enviou a mensagem
        String text = bot.messages[i].text;                           //Obtem o texto
        String from_name = bot.messages[i].from_name;
        if (from_name == "") 
            from_name = "Convidado";

        //Possiveis ações enviadas pelo Telegram
        if (text == "Timer" || text == "/timer"){
            bot.sendMessage(chat_id, "Quantas irrigações teremos? (no máximo 10 irrigações)", "");
            contexto = "timer";
        } else if(contexto=="timer"){
            if (text.toInt() > 0){
                timerN = text.toInt();
                bot.sendMessage(chat_id, "Ok! Serão "+String(timerN)+" irrigações. Informe o tempo de duração das irrigações em minutos (de 1 a 60)", "");
                contexto="duracao";
            } else {
                bot.sendMessage(chat_id, "Você deve informar um número de 1 a 99", "");
            }          
        } else if (text == "/temperatura"){
            String param = "temperature\":";
            String temperatura = leParamClimaTempo(param, urlTemp);
            bot.sendMessage(chat_id, "Temperatura atual: " + temperatura, "");
        } else if (text == "/chuva"){
            String param = "precipitation\":";
            String chuva = leParamClimaTempo(param, urlRain);
            bot.sendMessage(chat_id, "Precipitação atual: " + chuva, "");        
        } else if (contexto=="duracao"){
            if (text.toInt() > 0){
                timerDuracao = text.toInt();
                bot.sendMessage(chat_id, "Ok! Duração de "+String(timerDuracao)+" minutos. Agora informe o horário das "+String(timerN)+" irrigações. No formato hh:mm (ex: 16:30) em mensagens separadas.", "");
                contexto="schedule";
                contextoSchedule=0;
            } else {
                bot.sendMessage(chat_id, "Você deve informar um número de 1 a 60 como minutos para irrigação", "");
            }                      
        } else if (contexto=="schedule"){
            if (text.length()==5){
                horaArray[contextoSchedule] = text.substring(0, 2).toInt();
                minutoArray[contextoSchedule] = text.substring(3, 5).toInt();
                contextoSchedule++;                
                bot.sendMessage(chat_id, "Timer #"+String(contextoSchedule)+" programado com início às "+text+" horas.", "");
                if(contextoSchedule==timerN){
                    bot.sendMessage(chat_id, "Obrigado! Todos os timers foram programados! Para saber os horários programados utilizar o comando /estado", "");
                    programaArduino();
                    contexto="";                    
                }else{
                    bot.sendMessage(chat_id, "Favor informe o horário do próximo timer", "");
                }
            } else {
                bot.sendMessage(chat_id, "Informe o horário de início da irrigação #" + String(contextoSchedule+1) + " no formato hh:mm (ex: 16:30).", "");
            }
        } else if (text == "/ativa") {     
            SistemStatus = 1;
            bot.sendMessage(chat_id, "Sistema ativado por "+from_name, "");
        } else if (text == "Oi" || text == "oi"){            
            bot.sendMessage(chat_id, "Oi "+from_name+"!", "");
        } else if (text == "Liga" || text == "/liga"){          
            digitalWrite(MOTOR, HIGH);
            MotorStatus = 1;
            acionadoPeloUsuario = 1;        
            bot.sendMessage(chat_id, "Equipamento ligado", "");            
        } else if (text == "Desliga" || text == "/desliga"){
            digitalWrite(MOTOR, LOW);
            MotorStatus = 0;        
            bot.sendMessage(chat_id, "Equipamento desligado pelo usuário", "");
            acionadoPeloUsuario=0;            
        } else if (text == "Desativa" || text == "/desativa"){
            SistemStatus = 0;    
            bot.sendMessage(chat_id, "Sistema desativado", "");
        } else if (text == "/options") {
            String keyboardJson = "[[\"/ativa\", \"/desativa\"], [\"/liga\", \"/desliga\"], [\"/timer\", \"/tempos\"], [\"/temperatura\", \"/estado\"]]";
            bot.sendMessageWithReplyKeyboard(chat_id, "Escolha uma das opções", "", keyboardJson, true);       
        } else if (text == "Estado" || text=="/estado"){
            String Estado;        
            if(SistemStatus)
                Estado="Sistema ativado.\n"; 
            else
                Estado="Sistema desativado.\n";     
                
            if(MotorStatus){
                if(acionadoPeloUsuario)
                    Estado+="Irrigação ligada por ação do usuário.\n";                 
                else
                    Estado+="Irrigação ligada por tempo.\n"; 
            } else {      
                Estado+="Irrigação desligada.\n";     
            }

            Estado+="Hora "+String(hora)+":"+String(minuto)+"\n";   
            Estado+="\*****PROGRAMAÇÃO*****\n";
            Estado+=" Numero de irrigações: " + String(N)+"\n";
            for (int j=1;j<=N; j++) 
                Estado+="Inicio: "+String(H[j-1])+":"+String(M[j-1])+" Duração "+String(D[j-1])+"\n";   
            bot.sendMessage(chat_id,Estado, "");
        } else if (text == "/start") {
            String welcome = "IRRIGAÇÃO COMPUTADORIZADA. \n";
            welcome += "Versão 2.1 13/11/2019\n"; 
            welcome += "Ativa : Para ativar o sistema.\n";
            welcome += "Desativa : Para desativar o sistema.\n";
            welcome += "Estado : Retorna o estado sistema\n";
            welcome += "Liga: Liga o dispositivo imediatamente.\n";
            welcome += "Desliga: Desliga o dispositivo.\n";            
            welcome += "Tempos : Programa periodos de irrigacao. Envia os dados de cada periodo em linhas separadas\n";
            welcome += "Formato: hh:mm-dd onde hh=hora mm=minuto e pp=duracao em minutos\n";
            bot.sendMessage(chat_id, welcome, "Markdown");
        } else if (text.startsWith("Tempos") || text.startsWith("/tempos")) { //*********Recebe dados de programação******
            EEPROM.begin(128); 
            N = text.substring(7, 8).toInt();        //Obtem o número de periodos, caractere 8 da mensagem  
            Serial.println("N: "+N);
            EEPROM.put(0,N);                       //Cada int ocupa 4 posições na EEPROM
            int k=4;
            int pos=9;
            for (int j=0;j<N; j++){                   //Para cada dose
                H[j] = text.substring(pos, pos+2).toInt();         //obtem a hora inicial
                Serial.print("Hora inicial");
                Serial.println(H[j]);
                EEPROM.put(k,H[j]);                               //armazena horario inicial 
                k=k+4;
                pos=pos+3;
                M[j] = text.substring(pos, pos+2).toInt();         //obtem a minuto inicial
                Serial.print("Minuto inicial");
                Serial.println(M[j]);
                EEPROM.put(k,M[j]);                                 //armazena minuto inicial
                L[j]=60*H[j]+M[j];                                  //Calcula hora inicial em mminutos
                k=k+4;
                pos=pos+3;
                D[j] = text.substring(pos, pos+2).toInt();         //Duracao do periodo em minutos
                Serial.print("Duração");
                Serial.println(D[j]);
                EEPROM.put(k,D[j]);                               //armazena horario final
                k=k+4;
                pos=pos+3; 
            }
            EEPROM.commit();
            boolean ok = EEPROM.commit();
            Serial.println((ok) ? "Commit OK" : "Commit fallhou");      
            bot.sendMessage(chat_id,text, "");
        } else { //Não entrou em nenhum comando conhecido.
            bot.sendMessage(chat_id,"Não conheço esse comando.", "");
        }
    } //Fim do loop de mensagens
}//Fim handleMessages

void recupera_EEPROM(){
    EEPROM.begin(128);             //Abre 128 bytes da EEPROM para leitura e escrita
    Serial.println("Recuperando dados da EEPROM\n");
    EEPROM.get(0,N);             //Recupera numero de doses diarias
    Serial.print("N ");
    Serial.println(N);
    int k=4;                          //Cada int ocupa 4 bytes da eeprom                             
    //Recupera instante do inicio das doses
    for (int j=0;j<=N-1;++j){
        EEPROM.get(k,H[j]);                   //Recupera hora
        Serial.print("Hora inicial");
        Serial.println(H[j]);
        k=k+4;
        EEPROM.get(k,M[j]);                  //Recupera minuto 
        Serial.print("Minuto inicial");
        Serial.println(M[j]);
        k=k+4;                          
        EEPROM.get(k,D[j]);                //Rcupera 
        Serial.print("Duração ");
        Serial.println(D[j]);
        k=k+4;
        L[j]=60*H[j]+M[j];
    }
    EEPROM.end();                   //Fecha a EEPROM para leitura e escrita   
}


void setup() {
    Serial.begin(115200);
    Serial.println("Iniciando...");
    WiFi.mode(WIFI_STA);        //Importante para limpar dados de conexões
    WiFi.begin(ssid, password);
    pinMode(MOTOR, OUTPUT); //  Inicializa pino de controle do motor como saida
    delay(10);
    digitalWrite(MOTOR, LOW); // Motor desligado
    
    while (WiFi.waitForConnectResult() != WL_CONNECTED){
        Serial.println("Conexao falhou! Reiniciando...");
        delay(5000);
        ESP.restart();
    }

    // ********************Incialização OTA ***********************  
    ArduinoOTA.onStart([]() {
        Serial.println("Inicio...");
    });
    ArduinoOTA.onEnd([]() {
        Serial.println("nFim!");
    });
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        Serial.printf("Progresso: %u%%r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("Erro [%u]: ", error);
        if (error == OTA_AUTH_ERROR) Serial.println("Autenticacao Falhou");
        else if (error == OTA_BEGIN_ERROR) Serial.println("Falha no Inicio");
        else if (error == OTA_CONNECT_ERROR) Serial.println("Falha na Conexao");
        else if (error == OTA_RECEIVE_ERROR) Serial.println("Falha na Recepcao");
        else if (error == OTA_END_ERROR) Serial.println("Falha no Fim");
    });
    ArduinoOTA.begin();
    //********************fim inicialização OTA ********************
    
    Serial.println("Versao 2.0 14/01/2019");
    Serial.print("Endereco IP: ");
    Serial.println(WiFi.localIP()); 
    //Inicializa tempo
    configTime(timezone * 3600, dst * 0, "pool.ntp.org", "time.nist.gov");
    Serial.println("\nAguardando aquisição de hora e data");
    while (!time(nullptr)) {
        Serial.print(".");
        delay(1000);
    }
    Serial.println("\nData e hora adquiridas");
    SistemStatus=1;               //Inicializa sistema ativado
    recupera_EEPROM();
    client.setInsecure();
} //fim setup

void loop(){
    ArduinoOTA.handle();
    time_t now;
    struct tm * timeinfo;
    long int z;
    time(&now);
    timeinfo = localtime(&now);
    hora=(timeinfo->tm_hour);               //Obtem a hora
    minuto=(timeinfo->tm_min);              //e o minuto atual
    TM=hora*60+minuto;                   //Especifica o tempo em minutos
    int b=0, horaLigado=0;
    for (int i=0;i<=N-1;++i){          
        if(TM>=L[i] && TM<=L[i]+D[i] && SistemStatus==1){           //Se ja passou a hora de ligar
            Serial.println("Ligando motor por estar na hora!");
            digitalWrite(MOTOR, HIGH);         //Liga motor
            MotorStatus = 1;
            horaLigado = 1;
        }      
    }

    if(horaLigado==0 && acionadoPeloUsuario!=1){ //Não desliga, mesmo em horário que deveria estar desligado se foi acionado pelo usuário
        digitalWrite(MOTOR, LOW);         //Desliga motor  
        //Pini=analogRead(A0); 
        MotorStatus = 0;
    }
 
    if (millis() > Bot_lasttime + Bot_mtbs){
        int numNewMessages = bot.getUpdates(bot.last_message_received + 1);     //Determina o numero de mensagens recebidas
        while(numNewMessages){
            Serial.println("Mensagem recebida");
            handleNewMessages(numNewMessages);
            numNewMessages = bot.getUpdates(bot.last_message_received + 1);
        }
        Bot_lasttime = millis();
    }
}//fim loop

//Função que recupera horários armazenados nos vetores e programa o agendamento das irrigações
void programaArduino(){
    EEPROM.begin(128); 
    N = timerN;                            //Obtem o número de periodos
    EEPROM.put(0,N);                       //Cada int ocupa 4 posições na EEPROM
    int k=4;
    for (int j=0;j<N; j++){                   //Para cada dose
        H[j] = horaArray[j];
        Serial.println("Hora inicial: "+H[j]);
        EEPROM.put(k, horaArray[j]);                       //armazena horario inicial 
        k=k+4;

        M[j] = minutoArray[j];         
        Serial.println("Minuto inicial: "+M[j]);
        EEPROM.put(k,minutoArray[j]);                      //armazena minuto inicial
        L[j]=60*horaArray[j]+minutoArray[j];               //Calcula hora inicial em mminutos
        k=k+4;
       
        D[j] = timerDuracao;                               //Duracao do periodo em minutos
        Serial.println("Duração: "+D[j]);
        EEPROM.put(k,D[j]);                               //armazena horario final
        k=k+4;
    }
    EEPROM.commit();
    boolean ok = EEPROM.commit();
    Serial.println((ok) ? "Commit OK" : "Commit fallhou");      
    Serial.println("Arduino programado com os timers.");
}

String leParamClimaTempo(String param, String url){
  Serial.print("connecting to ");
  Serial.println(host);
  
  // Use WiFiClient class to create TCP connections
  WiFiClient client;
  const int httpPort = 80;
  if (!client.connect(host, httpPort)) {
    Serial.println("connection failed");
    return "falha ao conectar no clima tempo";
  } else{
    Serial.println("Conexão realizada.");
  }
  Serial.println("Requesting URL: "+url);
  
  // This will send the request to the server
  client.print(String("GET ") + url + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" + 
               "Connection: close\r\n\r\n");
  unsigned long timeout = millis();
  while (client.available() == 0) {
    if (millis() - timeout > 5000) {
      Serial.println(">>> Client Timeout !");
      client.stop();
      return "Timeout ao acessar climaTempo";
    }
  }
  
  // Read all the lines of the reply from server and print them to Serial
  while(client.available()){  
    String line = client.readStringUntil('\r');
    int i=0;
    while(line.indexOf(param) < 0 && i<20){
        line = client.readStringUntil('\r');
        delay(50);
        i++;
    }
    if(i==20)
      return "Timeout buscando dado";
    Serial.println(line);

    int pos = line.indexOf(param)+param.length();
    Serial.println("Pos: "+pos);
    int virgulaPos = line.indexOf(",", pos);
    Serial.println("VirgulaPos: "+virgulaPos);
    String temperatura = line.substring(pos, virgulaPos);
    Serial.print("Temperatura: "+temperatura);
    return temperatura;
  }
  Serial.println();
  Serial.println("closing connection");
}  

void saveE2PROM(){
    EEPROM.begin(128); 
    EEPROM.put(0,N);                       //Cada int ocupa 4 posições na EEPROM
    int k=4;
    int pos=9;
    for (int j=0;j<N; j++){                   //Para cada dose
        Serial.print("Hora inicial");
        Serial.println(H[j]);
        EEPROM.put(k,H[j]);                               //armazena horario inicial 
        k=k+4;
        Serial.print("Minuto inicial");
        Serial.println(M[j]);
        EEPROM.put(k,M[j]);                                 //armazena minuto inicial
        L[j]=60*H[j]+M[j];                                  //Calcula hora inicial em mminutos
        k=k+4;
        Serial.print("Duração");
        Serial.println(D[j]);
        EEPROM.put(k,D[j]);                               //armazena horario final
        k=k+4;
    }
    EEPROM.commit();
    boolean ok = EEPROM.commit();
    Serial.println((ok) ? "Commit OK" : "Commit fallhou");      
     
}
