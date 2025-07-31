import paho.mqtt.client as mqtt
import psycopg2
import datetime
import json
import unicodedata  
import string

MQTT_BROKER_URL = "192.168.18.73"
MQTT_USERNAME = "calebe"
MQTT_PASSWORD = "8811"
MQTT_TOPIC = "rfid/scanner/uid"
MQTT_TOPIC_NOT_FOUND = "rfid/scanner/uid/not_found"
MQTT_TOPIC_RESPONSE = "rfid/scanner/response"

DB_HOST = "192.168.18.10"
DB_PORT = "5432"
DB_NAME = "inventario_teste"
DB_USER = "gislenojr"
DB_PASS = "1234"


def limpar_para_lcd(texto):
    """Filtra o texto para conter apenas caracteres ASCII imprimíveis."""
    if texto is None:
        return ""
    
    nfkd_form = unicodedata.normalize('NFD', texto)
    sem_acentos = "".join([c for c in nfkd_form if not unicodedata.combining(c)])
    
    caracteres_permitidos = string.ascii_letters + string.digits + string.punctuation + ' '
    texto_final = "".join([c for c in sem_acentos if c in caracteres_permitidos])
    
    return texto_final


def conectar_banco():
    """Conecta ao banco de dados PostgreSQL na rede."""
    try:
        conn = psycopg2.connect(host=DB_HOST, port=DB_PORT, dbname=DB_NAME, user=DB_USER, password=DB_PASS)
        print(f"Conectado ao banco de dados PostgreSQL em {DB_HOST}")
        return conn
    except psycopg2.Error as e:
        print(f"Erro ao conectar ao banco de dados PostgreSQL: {e}")
        return None

def atualizar_status_item(conn, uid, mqtt_client):
    """
    Verifica o status, alterna, e envia respostas para os tópicos corretos,
    tratando os caracteres para o LCD.
    """
    if not conn:
        return
    timestamp_atual = datetime.datetime.now().replace(microsecond=0)
    uid_limpo = uid.strip()

    try:
        cursor = conn.cursor()
        sql_select = "SELECT nome, status FROM itens WHERE UPPER(TRIM(rfid)) = UPPER(%s)"
        cursor.execute(sql_select, (uid_limpo,))
        item = cursor.fetchone()

        if item:
            nome_item, status_atual = item[0], item[1]
            novo_status = "Emprestado" if status_atual == "Disponivel" else "Disponivel"

            sql_update = "UPDATE itens SET status = %s, ultima_atualizacao = %s WHERE UPPER(TRIM(rfid)) = UPPER(%s)"
            cursor.execute(sql_update, (novo_status, timestamp_atual, uid_limpo))
            conn.commit()
            print(f"✓ ATUALIZADO NO BANCO: Item '{nome_item}' alterado para '{novo_status}'.")

            nome_limpo_para_lcd = limpar_para_lcd(nome_item)
            status_limpo_para_lcd = limpar_para_lcd(novo_status)

            response_payload_lcd = json.dumps({"nome": nome_limpo_para_lcd, "status": status_limpo_para_lcd})
            mqtt_client.publish(MQTT_TOPIC_RESPONSE, response_payload_lcd)
            print(f"✓ Resposta de sucesso ('{nome_limpo_para_lcd}', '{status_limpo_para_lcd}') enviada para o ESP32.")

        else:
            print(f"✗ Item não encontrado no banco para o UID: {uid_limpo}")
            
            response_payload_lcd = json.dumps({"erro": "Nao cadastrado"})
            mqtt_client.publish(MQTT_TOPIC_RESPONSE, response_payload_lcd)
            print(f"✓ Resposta de 'não cadastrado' enviada para o ESP32.")

            response_payload_notfound = json.dumps({"uid": uid_limpo, "hora": timestamp_atual.strftime("%H:%M:%S")})
            mqtt_client.publish(MQTT_TOPIC_NOT_FOUND, response_payload_notfound)
            print(f"✓ Alerta de UID não encontrado enviado para o tópico '{MQTT_TOPIC_NOT_FOUND}'.")
        
        cursor.close()
    except psycopg2.Error as e:
        print(f"✗ Erro ao interagir com o banco de dados: {e}")
        conn.rollback()

def on_message(client, userdata, msg):
    json_string = msg.payload.decode("utf-8")
    print(f"\nMensagem JSON recebida no tópico '{msg.topic}': {json_string}")
    try:
        data = json.loads(json_string)
        uid_recebido = data['uid']
        print(f"UID extraído do JSON: {uid_recebido}")
        db_connection = userdata['db_conn']
        mqtt_client = userdata['mqtt_client']
        atualizar_status_item(db_connection, uid_recebido, mqtt_client)
    except (json.JSONDecodeError, KeyError) as e:
        print(f"Erro ao processar JSON: {e}")

def on_connect(client, userdata, flags, rc, properties=None):
    if rc == 0:
        print("Conectado ao Broker MQTT com sucesso!")
        client.subscribe(MQTT_TOPIC)
        print(f"Inscrito no tópico: {MQTT_TOPIC}")
    else:
        print(f"Falha ao conectar, código de retorno: {rc}\n")

if __name__ == "__main__":
    db_conn = conectar_banco()
    if not db_conn:
        exit(1)
    
    user_data = {'db_conn': db_conn}
    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, userdata=user_data)
    
    client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)
    client.on_connect = on_connect
    client.on_message = on_message
    
    client.user_data_set(user_data)
    client._userdata['mqtt_client'] = client

    try:
        print("Tentando conectar ao broker MQTT...")
        client.connect(MQTT_BROKER_URL, 1883, 60)
        client.loop_forever()
    except KeyboardInterrupt:
        print("\nPrograma interrompido.")
    except Exception as e:
        print(f"Ocorreu um erro: {e}")
    finally:
        if db_conn:
            db_conn.close()
            print("Conexão com o banco de dados fechada.")