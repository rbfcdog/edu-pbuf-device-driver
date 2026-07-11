# Comandos para rodar o edu_chat

```sh
cd /home/rodrigodog/linux
sudo rmmod edu_chat 2>/dev/null || true

make -C /lib/modules/$(uname -r)/build \
  M=$PWD/drivers/lkcamp \
  CONFIG_EDU_CHAT=m \
  KCFLAGS=-I$PWD/include \
  modules

make -C tools/testing/selftests/edu_chat

sudo insmod drivers/lkcamp/edu_chat.ko capacity=4096
sudo chmod 666 /dev/edu_chat

# chat bidirecional - abrir em dois terminais
./tools/testing/selftests/edu_chat/edu_chat_client /dev/edu_chat rodrigo
./tools/testing/selftests/edu_chat/edu_chat_client /dev/edu_chat ana

# teste automatizado
./tools/testing/selftests/edu_chat/edu_chat_test /dev/edu_chat
```
