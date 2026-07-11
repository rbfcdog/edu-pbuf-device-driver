# `edu_pbuf`: pseudo driver de dispositivo para Linux

`edu_pbuf` é um pseudo driver de dispositivo educacional para o Kernel Linux.
Ele cria o character device `/dev/edu_pbuf` e simula um pequeno canal de
mensagens entre user space e kernel space.

A função principal do driver é manter um buffer dentro do kernel. Dados escritos
em `/dev/edu_pbuf` são copiados de user space para kernel space com
`copy_from_user()`. Depois, esses dados podem ser lidos de volta por user space
com `copy_to_user()`.

Além da funcionalidade mínima de buffer, o driver demonstra interfaces usadas em
drivers reais:

- comandos `ioctl()` para consulta e configuração estruturada;
- atributos `sysfs` para configuração simples e diagnóstico;
- suporte a `poll/select`;
- modos configuráveis por flags;
- leitura bloqueante opcional;
- estatísticas internas de uso.

## O que o driver demonstra

- Carregamento dinâmico de módulo com `insmod`, `lsmod` e `rmmod`.
- Integração in-tree com `Kconfig` e `Makefile`.
- Organização em múltiplos arquivos: core, file operations e `sysfs`.
- Registro de character device com `alloc_chrdev_region()` e `cdev`.
- Criação automática de `/dev/edu_pbuf` com `class_create()` e
  `device_create()`.
- Transferência segura de dados entre user space e kernel space com
  `copy_from_user()` e `copy_to_user()`.
- Configuração em tempo de execução com `ioctl()`.
- Modos append, clear-on-read e blocking-read.
- Prontidão de leitura/escrita com `poll/select` e `wait_queue`.
- Atributos `sysfs` para configuração e diagnóstico.
- Estatísticas internas expostas por `ioctl()` e `sysfs`.

## Organização do módulo

O módulo final continua sendo `edu_pbuf.ko`, mas a implementação foi separada em
arquivos menores:

```text
edu_pbuf_core.c      inicialização, saída, cdev, /dev/edu_pbuf e sysfs group
edu_pbuf_fops.c      open, release, read, write, ioctl e poll
edu_pbuf_sysfs.c     atributos limit, flags, length, stats e clear
edu_pbuf_internal.h  estado compartilhado e declarações internas
```

## Arquivos do repositório

```text
drivers/lkcamp/Kconfig
drivers/lkcamp/Makefile
drivers/lkcamp/edu_pbuf_core.c
drivers/lkcamp/edu_pbuf_fops.c
drivers/lkcamp/edu_pbuf_sysfs.c
drivers/lkcamp/edu_pbuf_internal.h
include/uapi/linux/edu_pbuf.h
tools/testing/selftests/edu_pbuf/Makefile
tools/testing/selftests/edu_pbuf/edu_pbuf_test.c
kernel-integration.patch
```

Este repositório não inclui a pasta `docs/` do projeto principal.

## Aplicando em uma árvore do Kernel Linux

A partir da raiz de uma árvore compatível do Kernel Linux:

```sh
cp -r /path/to/edu-pbuf-device-driver/drivers/lkcamp drivers/
cp /path/to/edu-pbuf-device-driver/include/uapi/linux/edu_pbuf.h include/uapi/linux/
mkdir -p tools/testing/selftests/edu_pbuf
cp /path/to/edu-pbuf-device-driver/tools/testing/selftests/edu_pbuf/* \
    tools/testing/selftests/edu_pbuf/
git apply /path/to/edu-pbuf-device-driver/kernel-integration.patch
```

## Compilação

Para compilar pela própria árvore do kernel:

```sh
./scripts/config -m EDU_PBUF
make olddefconfig
make -j"$(nproc)" drivers/lkcamp/edu_pbuf.ko
make -C tools/testing/selftests/edu_pbuf
```

Para compilar contra o kernel que está rodando no host:

```sh
make -C /lib/modules/$(uname -r)/build \
  M=$PWD/drivers/lkcamp \
  CONFIG_EDU_PBUF=m \
  KCFLAGS=-I$PWD/include \
  modules

make -C tools/testing/selftests/edu_pbuf
```

## Execução

Use uma VM ou sistema rodando a mesma versão/configuração do kernel usada para
compilar o módulo. Se o `vermagic` do módulo não corresponder ao `uname -r`, o
`insmod` pode falhar com `Invalid module format`.

Carregar o módulo:

```sh
sudo insmod drivers/lkcamp/edu_pbuf.ko capacity=4096
lsmod | grep edu_pbuf
ls -l /dev/edu_pbuf
sudo dmesg -T | tail -n 20
```

Teste básico de escrita e leitura:

```sh
printf 'mensagem do usuario\n' | sudo tee /dev/edu_pbuf
sudo cat /dev/edu_pbuf
```

Teste com `ioctl()`:

```sh
sudo tools/testing/selftests/edu_pbuf/edu_pbuf_test /dev/edu_pbuf
```

Inspecionar atributos `sysfs`:

```sh
ls -l /sys/class/edu_pbuf/edu_pbuf/
cat /sys/class/edu_pbuf/edu_pbuf/limit
cat /sys/class/edu_pbuf/edu_pbuf/flags
cat /sys/class/edu_pbuf/edu_pbuf/length
cat /sys/class/edu_pbuf/edu_pbuf/stats
```

Ativar modo append por `sysfs`:

```sh
echo 0x1 | sudo tee /sys/class/edu_pbuf/edu_pbuf/flags
printf 'parte A ' | sudo tee /dev/edu_pbuf
printf 'parte B\n' | sudo tee /dev/edu_pbuf
sudo cat /dev/edu_pbuf
echo clear | sudo tee /sys/class/edu_pbuf/edu_pbuf/clear
```

Remover o módulo:

```sh
sudo rmmod edu_pbuf
sudo dmesg -T | tail -n 20
```

## Resumo

Para o usuário, `edu_pbuf` é um dispositivo virtual em `/dev/edu_pbuf` que
permite escrever uma mensagem, armazená-la em um buffer dentro do kernel e
recuperá-la depois. O objetivo é demonstrar, de forma prática, como um character
device Linux troca dados com user space e como um driver pode expor interfaces
de configuração por `ioctl()` e `sysfs`.
