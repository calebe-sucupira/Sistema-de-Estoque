import paho.mqtt.client as mqtt
import psycopg2
import datetime
import json

MQTT_BROKER_URL = "192.168.18.73"
MQTT_USERNAME = "calebe"
MQTT_PASSWORD = "8811"
MQTT_TOPIC = "rfid/scanner/uid"
MQTT_TOPIC_NOT_FOUND = "rfid/scanner/uid/not_found"

DB_HOST = "192.168.18.10"
DB_PORT = "5432"
DB_NAME = "inventario_teste"
DB_USER = "gislenojr"
DB_PASS = "1234"

def conectar_banco():
    """Conecta ao banco de dados PostgreSQL na rede."""
    try:
        conn = psycopg2.connect(
            host=DB_HOST,
            port=DB_PORT,
            dbname=DB_NAME,
            user=DB_USER,
            password=DB_PASS
        )
        print(f"Conectado ao banco de dados PostgreSQL em {DB_HOST}")
        return conn
    except psycopg2.Error as e:
        print(f"Erro ao conectar ao banco de dados PostgreSQL: {e}")
        return None

def atualizar_status_item(conn, uid, mqtt_client):
    """
    Verifica o status atual de um item, o alterna, e atualiza o banco de dados
    com o timestamp sem os microssegundos.
    """
    if not conn:
        print("Não há conexão com o banco de dados para atualizar.")
        return

    # =======================================================
    # ==                 MUDANÇA APLICADA AQUI             ==
    # =======================================================
    # Sua sugestão, perfeitamente aplicada para remover os microssegundos.
    timestamp_atual = datetime.datetime.now().replace(microsecond=0)
    uid_limpo = uid.strip()

    try:
        cursor = conn.cursor()
        
        sql_select = "SELECT rfid, status FROM itens WHERE UPPER(TRIM(rfid)) = UPPER(%s)"
        cursor.execute(sql_select, (uid_limpo,))
        item = cursor.fetchone()

        if item:
            status_atual = item[1]
            novo_status = ""

            print(f"✓ Item encontrado: UID '{uid_limpo}', Status Atual: '{status_atual}'")

            if status_atual == "Disponível":
                novo_status = "Emprestado"
            elif status_atual == "Emprestado":
                novo_status = "Disponível"
            else:
                print(f"  - Status '{status_atual}' não é padrão. Alterando para 'Emprestado'.")
                novo_status = "Emprestado"
            
            sql_update = "UPDATE itens SET status = %s, ultima_atualizacao = %s WHERE UPPER(TRIM(rfid)) = UPPER(%s)"
            cursor.execute(sql_update, (novo_status, timestamp_atual, uid_limpo))
            
            conn.commit()
            print(f"✓ ATUALIZADO NO BANCO: Status do item '{uid_limpo}' alterado de '{status_atual}' para '{novo_status}'.")

        else:
            print(f"✗ Item não encontrado no banco para o UID: {uid_limpo}")
            hora_formatada = timestamp_atual.strftime("%H:%M:%S")
            message_payload = json.dumps({"uid": uid, "hora_leitura": hora_formatada})
            mqtt_client.publish(MQTT_TOPIC_NOT_FOUND, message_payload)
            print(f"✓ UID '{uid_limpo}' publicado no tópico de não encontrados com a hora '{hora_formatada}'")
        
        cursor.close()
    except psycopg2.Error as e:
        print(f"✗ Erro ao interagir com o banco de dados: {e}")
        conn.rollback()


def on_message(client, userdata, msg):
    """Função chamada quando uma mensagem JSON é recebida do broker."""
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

    client = mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, userdata={'db_conn': db_conn})
    client.username_pw_set(MQTT_USERNAME, MQTT_PASSWORD)
    client.on_connect = on_connect
    client.on_message = on_message
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