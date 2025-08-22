#pragma once

// Embedded clientinfo.xml content loaded virtually
static const char kClientInfoXml[] = R"(<?xml version="1.0" encoding="euc-kr" ?>
<clientinfo>
   <desc>Ragnarok Client Information</desc>
   <servicetype>korea</servicetype>
   <servertype>primary</servertype>
   <connection>
      <display>RagnaPH</display>
            <address>server.ragna.ph</address>
            <port>6901</port>
            <version>55</version>
            <langtype>11</langtype>
<registrationweb>https://ragna.ph/?module=account&action=create</registrationweb>
<loading>
<image>loading00.jpg</image>
<image>loading01.jpg</image>
<image>loading02.jpg</image>
<image>loading03.jpg</image>
<image>loading04.jpg</image>
</loading>
<aid>
            <admin>2000000</admin>
<admin>2000001</admin>
<admin>2000002</admin>
<admin>2000003</admin>
<admin>2000004</admin>
<admin>2000005</admin>
        </aid>
      </connection>
</clientinfo>
)";
