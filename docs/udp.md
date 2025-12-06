# UDP Taşıma Kılavuzu

Bu döküman, iletişim kütüphanesi içindeki `comm::ethernet::UDP` taşıyıcısının nasıl yapılandırılıp kullanılacağına dair ayrıntılı bilgi verir. Yapılandırma seçeneklerini, yaşam döngüsünü, iş parçacığı davranışını ve sık karşılaşılan sorunları çözmek için ipuçlarını içerir.

## Genel Bakış

`comm::ethernet::UDP`, ortak `ICommunication` arayüzünü implement eden POSIX uyumlu bir UDP soketidir. `comm::ethernet::UDPConfig` üzerinden yapılandırılır; `open()` ile açılır ve ardından `send()`/`receive()` çağrılarıyla paket gönderip alabilirsiniz.

Temel özellikler:

- Belirli bir yerel arayüze/porta bağlanabilme.
- Varsayılan uzak uç noktası tanımlama veya multicast/broadcast modlarında çalışma.
- Ayarlanabilir bekleme süresine sahip isteğe bağlı arka plan alım döngüsü.
- İstatistik takibi ve temel sağlık durumunun merkezi altyapı ile entegre edilmesi.

## Yapılandırma

Bir `UDPConfig` oluşturup senaryonuz için gerekli alanları doldurun. Belirtilmeyen değerler varsayılanlara döner.

```cpp
comm::ethernet::UDPConfig config;
config.local.address = "0.0.0.0";   // Varsayılan bağlanma adresi (INADDR_ANY)
config.local.port = 5000;            // Yerel port
config.remote.address = "192.168.1.10"; // Varsayılan hedef
config.remote.port = 6000;
config.allowBroadcast = false;      // Broadcast göndermek için true
config.joinMulticast = false;       // Multicast grubuna katılım
config.multicastGroup = "239.0.0.1"; // Katılınacak multicast adresi
config.multicastTTL = 1;            // Multicast hop sayısı
config.multicastLoopback = false;   // Gönderilen paketlerin geri alınması
config.autoRebind = true;           // Sağlık bozulduğunda soketi yeniden açmayı dener
config.nonBlocking = false;         // Soketi bloklamayan moda alır
config.direction = comm::Direction::TwoWay; // Çift yönlü iletişim
config.spawnReceiveThread = true;   // Arka plan alım döngüsü otomatik başlasın
config.receiveThreadSleep = std::chrono::milliseconds{5}; // Boşta bekleme süresi
config.timeouts.receiveTimeout = std::chrono::milliseconds{100};
config.timeouts.sendTimeout = std::chrono::milliseconds{100};
```

Genel `Config` alanları (`timeouts`, `retryPolicy`, `logging`, `autoReconnect` vb.) aynı şekilde kullanılabilir.

## Yaşam Döngüsü

1. Yapılandırma ile birlikte taşıyıcıyı oluşturun:
   ```cpp
   comm::ethernet::UDP udp{config};
   ```

2. Arka plan alım döngüsünü kullanacaksanız geri çağrıları kaydedin:
   ```cpp
   udp.setReceiveCallback([](const comm::ByteVector& data) {
       // Gelen datagramı ele alın
   });

   udp.setErrorCallback([](const comm::Error& error) {
       // Hataları loglayın veya işleyin
   });
   ```

3. Taşıyıcıyı açın:
   ```cpp
   if (!udp.open()) {
       // Hata detayları callback üzerinden gelir
   }
   ```

4. Veri gönderip alın:
   ```cpp
   comm::ByteVector payload{0xDE, 0xAD, 0xBE, 0xEF};
   udp.send(payload);

   comm::ByteVector rx;
   udp.receive(rx, 1024); // Yalnızca dahili iş parçacığı kapalıysa gereklidir
   ```

5. İşiniz bitince kapatın:
   ```cpp
   udp.close();
   ```

Tüm işlemler iş parçacığı güvenlidir. Kod exceptions fırlatmaz; hatalar dönüş değerleri ve error callback üzerinden bildirilir.

## İş Parçacığı Davranışı

Varsayılan olarak UDP taşıyıcısı aşağıdaki koşullar sağlandığında dahili bir alım döngüsü başlatır:

- Taşıyıcı açık.
- `config.spawnReceiveThread` true.
- Bir receive callback kayıtlı.
- Yön (`direction`) alımı destekliyor.

Döngü `receive()` çağrıları yapar ve gelen veriyi callback’e iletir. Veri yoksa `config.receiveThreadSleep` süresi kadar uyur (varsayılan 5 ms).

`config.spawnReceiveThread = false` yaparak bu döngüyü devre dışı bırakabilirsiniz. Bu modda `receive()` çağrılarını kendi iş parçacığınızdan yapmanız gerekir; mevcut planlayıcıya entegre etmek için idealdir.

## Multicast ve Broadcast Notları

- Broadcast göndermek için `allowBroadcast` değerini true yapın (örn. `255.255.255.255`).
- Multicast almak istiyorsanız `joinMulticast = true` ve `multicastGroup` alanını doldurun. Soket `IP_ADD_MEMBERSHIP` ile gruba katılır.
- `multicastLoopback` ile soketin kendi gönderdiği multicast paketlerini alıp almayacağını belirleyebilirsiniz.

## Sağlık İzleme ve Yeniden Bağlanma

`autoRebind` açık olduğunda, hafif bir izleyici uzun süreli hareketsizlik veya hataları tespit ederse soketi kapatıp `timeouts.reconnectInterval` sonrasında yeniden açmayı dener. Böylece geçici ağ sorunları minimum kodla tolere edilir.

## İstatistik ve Teşhis

Toplam metrikleri görmek için `getStatistics()` çağırın:

```cpp
const auto stats = udp.getStatistics();
std::cout << "Gönderilen byte: " << stats.bytesSent << "\n";
std::cout << "Alınan byte: " << stats.bytesReceived << "\n";
std::cout << "Hata sayısı: " << stats.errorCount << "\n";
```

`isHealthy()` fonksiyonu taşıyıcının kendini sağlıklı görüp görmediğini döndürür. Hatalar meydana geldiğinde `comm::Error` nesnesi error callback’e iletilir.

## Sorun Giderme

- **`open()` başarısız:** Bağlanmaya çalıştığınız adres/port’un boş olduğundan ve soket açma yetkiniz olduğundan emin olun. Ayrıntılar error callback üzerinden gelir.
- **Veri alınmıyor:** Yön ayarının alıma izin verdiğini, `spawnReceiveThread` değerinin kullanımınıza uygun olduğunu ve uzak tarafın doğru IP/port’u hedeflediğini kontrol edin.
- **Geçersiz uzak adres:** `send()`, `remote.address` boşsa `multicastGroup` değerine düşer. Alanlardan en az birinin dolu olduğundan emin olun.
- **Multicast katılım hatası:** İşletim sisteminin ilgili multicast adresini ve arayüzünü desteklediğini doğrulayın.

## Minimal Örnek

```cpp
#include <comm/ethernet/udp.hpp>
#include <iostream>

int main() {
    comm::ethernet::UDPConfig config;
    config.local.port = 5000;
    config.remote.address = "127.0.0.1";
    config.remote.port = 5001;

    comm::ethernet::UDP udp{config};
    udp.setErrorCallback([](const comm::Error& error) {
        std::cerr << "UDP hatası: " << error.message << '\n';
    });

    if (!udp.open()) {
        return 1;
    }

    comm::ByteVector payload{'p', 'i', 'n', 'g'};
    udp.send(payload);

    // Dahili iş parçacığı devre dışıysa manuel alım
    // comm::ByteVector rx;
    // udp.receive(rx, 1024);

    udp.close();
    return 0;
}
```

Bu kılavuz, UDP taşıyıcısını yapılandırma, kullanma ve sorun giderme süreçlerinde size yol gösterecektir. Ek örneklere ihtiyaç duyarsanız veya özel senaryolarla karşılaşırsanız bakım ekibine haber verin.
