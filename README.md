# `edu_chat`: chat educacional em um pseudo device driver Linux

`edu_chat` é um pseudo device driver educacional para o Kernel Linux. Ele cria
o character device `/dev/edu_chat` e simula uma sala de chat local entre
processos em user space.

A ideia é simples: um processo escreve uma mensagem em `/dev/edu_chat`, o
driver copia essa mensagem para um buffer em memória do kernel com
`copy_from_user()`, e outros processos recuperam as mensagens com `read()`,
usando `copy_to_user()` no caminho de volta.

Além do requisito mínimo de buffer, o driver demonstra recursos usados em
drivers reais:

- comandos `ioctl()` para consulta e configuração estruturada;
- atributos `sysfs` para configuração simples e diagnóstico;
- modo append para acumular mensagens como histórico de conversa;
- modo clear-on-read para consumir mensagens após leitura;
- leitura bloqueante para esperar novas mensagens;
- suporte a `poll()`/`select()`;
- estatísticas internas de uso;
- cliente interativo em C para troca de mensagens entre dois terminais.

Este repositório contém apenas o código do driver, a UAPI, os programas de
teste e o patch de integração. A pasta `docs/` do projeto principal não foi
incluída.

## Arquivos

```text
drivers/lkcamp/Kconfig
drivers/lkcamp/Makefile
drivers/lkcamp/edu_chat_core.c
drivers/lkcamp/edu_chat_fops.c
drivers/lkcamp/edu_chat_sysfs.c
drivers/lkcamp/edu_chat_internal.h
include/uapi/linux/edu_chat.h
tools/testing/selftests/edu_chat/Makefile
tools/testing/selftests/edu_chat/edu_chat_test.c
tools/testing/selftests/edu_chat/edu_chat_client.c
kernel-integration.patch
```

## O que cada parte faz

- `edu_chat_core.c`: inicialização, saída, alocação do buffer, registro do
  `cdev`, criação de `/dev/edu_chat` e integração com `sysfs`.
- `edu_chat_fops.c`: implementação de `open`, `release`, `read`, `write`,
  `poll` e `ioctl`.
- `edu_chat_sysfs.c`: atributos `limit`, `flags`, `length`, `stats` e `clear`.
- `edu_chat_internal.h`: declarações internas compartilhadas entre os arquivos
  do módulo.
- `edu_chat.h`: header UAPI usado pelo kernel e pelos programas de user space.
- `edu_chat_test.c`: selftest que valida leitura, escrita, `ioctl`, `poll`,
  flags e estatísticas.
- `edu_chat_client.c`: cliente interativo que transforma o driver em uma sala
  de chat entre dois terminais.

## Aplicando em uma árvore do Kernel Linux

A partir da raiz de uma árvore compatível do Kernel Linux:

```sh
cp -r /path/to/edu-pbuf-device-driver/drivers/lkcamp drivers/
cp /path/to/edu-pbuf-device-driver/include/uapi/linux/edu_chat.h include/uapi/linux/
mkdir -p tools/testing/selftests/edu_chat
cp /path/to/edu-pbuf-device-driver/tools/testing/selftests/edu_chat/* \
    tools/testing/selftests/edu_chat/
git apply /path/to/edu-pbuf-device-driver/kernel-integration.patch
```

## Compilação

Para compilar pela própria árvore do kernel:

```sh
./scripts/config -m EDU_CHAT
make olddefconfig
make -j"$(nproc)" drivers/lkcamp/edu_chat.ko
make -C tools/testing/selftests/edu_chat
```

Para compilar contra o kernel que está rodando no host:

```sh
make -C /lib/modules/$(uname -r)/build \
  M=$PWD/drivers/lkcamp \
  CONFIG_EDU_CHAT=m \
  KCFLAGS=-I$PWD/include \
  modules

make -C tools/testing/selftests/edu_chat
```

## Execução rápida

Use uma VM ou sistema rodando a mesma versão/configuração do kernel usada para
compilar o módulo. Se o `vermagic` do módulo não corresponder ao `uname -r`, o
`insmod` pode falhar com `Invalid module format`.

```sh
sudo rmmod edu_chat 2>/dev/null || true

sudo insmod drivers/lkcamp/edu_chat.ko capacity=4096

lsmod | grep edu_chat
ls -l /dev/edu_chat
sudo dmesg -T | tail -n 20
```

O driver configura o device node como `0666`, então os exemplos abaixo podem
ser executados sem `sudo` depois do `insmod`.

Teste uma escrita e uma leitura simples:

```sh
printf 'rodrigo: oi pessoal\n' > /dev/edu_chat
cat /dev/edu_chat
```

## Chat entre dois terminais

Antes de abrir os clientes, limpe o histórico e deixe apenas o modo append
ativo:

```sh
echo 4096 | sudo tee /sys/class/edu_chat/edu_chat/limit >/dev/null
echo 0x1 | sudo tee /sys/class/edu_chat/edu_chat/flags >/dev/null
echo clear | sudo tee /sys/class/edu_chat/edu_chat/clear >/dev/null
```

Terminal 1:

```sh
./tools/testing/selftests/edu_chat/edu_chat_client /dev/edu_chat rodrigo
```

Terminal 2:

```sh
./tools/testing/selftests/edu_chat/edu_chat_client /dev/edu_chat ana
```

O cliente abre `/dev/edu_chat` com `O_RDWR | O_NONBLOCK`, ativa append por
`ioctl()` e usa `poll()` para esperar ao mesmo tempo pelo teclado e pelo device.
Ao entrar, ele descarta silenciosamente o histórico antigo; depois disso, mostra
somente mensagens novas. No driver, escritas em modo append não resetam o cursor
de leitura do próprio descritor, evitando repetir a conversa inteira a cada
mensagem enviada. No terminal, o cliente também apaga a linha crua que acabou de
ser digitada e deixa visível apenas a versão formatada como `nickname: mensagem`.

Exemplo esperado:

```text
Terminal rodrigo:
rodrigo: ola
ana: oieee

Terminal ana:
ana: oieee
rodrigo: tudo bem?
```

## Teste com `ioctl()`

```sh
sudo tools/testing/selftests/edu_chat/edu_chat_test /dev/edu_chat
```

O selftest consulta informações, limpa o chat, ativa flags, escreve mensagens,
usa `poll()`, lê dados, altera o limite lógico e consulta estatísticas.

## Inspeção por `sysfs`

```sh
ls -l /sys/class/edu_chat/edu_chat/
cat /sys/class/edu_chat/edu_chat/limit
cat /sys/class/edu_chat/edu_chat/flags
cat /sys/class/edu_chat/edu_chat/length
cat /sys/class/edu_chat/edu_chat/stats
echo clear | sudo tee /sys/class/edu_chat/edu_chat/clear
```

## Remoção

```sh
sudo rmmod edu_chat
sudo dmesg -T | tail -n 20
```

Se aparecer `Module edu_chat is in use`, feche qualquer `cat /dev/edu_chat` ou
`edu_chat_client` aberto em outro terminal:

```sh
ps -eo pid,ppid,stat,comm,args | grep -E 'cat /dev/edu_chat|edu_chat_client' | grep -v grep
sudo pkill -f 'cat /dev/edu_chat'
sudo pkill -f 'edu_chat_client'
sudo rmmod edu_chat
```

## Resumo

Para o usuário, `edu_chat` é um dispositivo virtual em `/dev/edu_chat` que
funciona como uma sala de chat local. Para o projeto, ele demonstra os conceitos
centrais de um character device driver: módulo carregável, `cdev`, major/minor,
`file_operations`, cópia segura entre user space e kernel space, configuração
por `ioctl()`/`sysfs`, sincronização e espera por eventos com `poll()`.
