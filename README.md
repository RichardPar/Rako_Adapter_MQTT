# Rako_Adapter_MQTT

Created on Linux using CodeLite IDE and Qmake based.

#For the RAKO HUB Lighting controller to HomeAssistant MQTT interface<br>

This is a work in progress..... your mileage may vary... it was written in a day as a test.. it worked awesomely.
Error checking is minimal - it was quick! 

 
Working <br>
  * Config Discovery pushing to HomeAssistant<br>
  * Individual lights controllable (16 Channels)<br> 
  * 5 SCENE switches... SCENE 0 should be set to OFF<br>
  * Events from RAKO (Scene and lightint values) <br>
  
  
Rooms are to be configured from 1 to 32 (If you want more, then change the values) - Scenes are set in the RAKO unit

rako_adapter -r [RAKO ip address] -m [MQTT IP] -u [MQTT Username] -p [MQTT Password]


Product_Type:           Hub<br>
Product_HubId:          12345cad-254f-0000-beef-4d63deadbeef<br>
Product_MAC:            70:B3:D5:--:--:--<br>
Product_Version:        3.1.6<br>

Room 0 [LIGHT] House Master<br>
<br>
Room 1 [LIGHT] Hallway<br>
	Channel 1	Channel 1<br>
	Channel 2	test<br>
<br>
Room 2 [LIGHT] downstairs bathroom<br>
	Channel 1	Channel 1<br>
<br>
Room 3 [LIGHT] plant room<br>
	Channel 1	Channel 1<br>
<br>
Room 5 [LIGHT] dining<br>
	Channel 2	Dining room pendants<br>
	Channel 3	dining downlights<br>
	
![HomeAssistant](RAko.png)

