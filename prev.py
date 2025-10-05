#Geschreven door Jelle Haverlag
#Python 3.6.6

#Import libraries
import contextlib
with contextlib.redirect_stdout(None):
    import pygame #Pygame vindt het leuk om een welkombericht te geven, dus laat ik het stil importen
import colorama
from lib.helperFunctions import *
from sys import argv, version
from scipy.ndimage import zoom
from math import cos, pi, ceil
from cv2 import imwrite as save_image
from cv2 import resize
import numpy as np

#Colorama is zodat ik meer controle heb over de console output.
colorama.init()	

#Global variables
inputFilename  					= ""
outputFilename 					= ""
rawCompressedImageDataBinary	= ""
rawCompressedImageDataBytes		= bytes()
quantizationTableIDs			= list()
quantizationTableValues 		= list()
channelIdentifiers 				= list()	#Channel identifiers, 01 - Y, 02 - Cb, 03 - Cr
samplingFactors					= list()	#Sampling factors, 11 = 1x1, 21 = 2x1, 22 = 2x2, etc.
channelQuantizationTableIDs		= list()	#Welke quantization table bij welke channel hoort
binaryCodesTables				= list()	#Een lijst van alle binary code tabellen
binaryCodesTablesValues			= list()	#Een lijst van alle bijbehorende waardes
binaryCodesTablesIDs			= list()	#Een lijst van alle bijbehorende ID's
DCTchannelSelectorList			= list()	#Selectors
DCTtablesSelectorListDC			= list()	#Tabellen voor DC
DCTtablesSelectorListAC			= list()	#Tabellen voor AC
imageDataRange					= list()	#Begin-byte en eind-byte voor bitstream
spectralSelection				= list()	#Vrijwel altijd 0 .. 63
currentDCTvalues				= list()
currentBlock					= list()
blocks							= list()	#Alle blokken
DCTvaluesLeft					= 0
width							= 0
height							= 0
blockCount						= 0
readPosition					= 0
previousDCcomponents			= [0, 0, 0]
blockLength						= 0

debugLog = open("./debug_decoder.log", 'w')

#Begin van de code
def main():
	#Laad de variabelen
	global inputFilename
	global outputFilename
	global blocks
	#Lees arguments
	try: #Probeer het eerste argument te lezen (de input file)
		inputFilename = str(argv[1])
	except IndexError: #Als die niet bestaat, geef een error
		print ("Geen input gedefinieerd.")
		exit()
		
	try: #Probeer het tweede argument te lezen (de output file)
		outputFilename = str(argv[2])
	except IndexError: #Als die niet bestaat, verzin dan zelf een output naam
		if inputFilename.endswith(".jpg"):
			outputFilename = inputFilename.replace(".jpg", ".png") #Bijvoorbeeld input.jpg -> input.png
		else:
			outputFilename = inputFilename[:-4] + ".jpg" #Bijvoorbeeld input.png -> input.jpg
			
	#Debug Log
	debugLog.write(f"Input: {inputFilename}\n")
	debugLog.write(f"Output: {outputFilename}\n")
	
	#Kijk naar de extension van de input file
	if not (inputFilename.endswith(".jpg")): #Als het niet een JPG-bestand is
		print ("Ongeldige input.")
		debugLog.write ("\nOngeldige input.")
		exit()
	else: #Als het wel een JPG-bestand is
		decodeJPG()
		

#Decoderen van een JPG-bestand
def decodeJPG():
	#Gebruik deze variabelen van buiten de functie
	global quantizationTableIDs
	global quantizationTableValues
	global channelIdentifiers 			
	global samplingFactors				
	global channelQuantizationTableIDs
	global binaryCodesTables
	global binaryCodesTablesValues
	global binaryCodesTablesIDs
	global DCTchannelSelectorList
	global DCTtablesSelectorListDC
	global DCTtablesSelectorListAC
	global imageDataRange
	global spectralSelection
	global rawCompressedImageDataBinary
	global rawCompressedImageDataBytes
	global DCTvaluesLeft
	global currentDCTvalues
	global width
	global height
	global blockCount
	global inputFile
	global readPosition
	global blockLength
	#Open de input file in read binary mode
	inputFile = open(inputFilename, "rb") 
	
	#Begin van een JPG file verifiëren
	startOfImage = inputFile.read(2)
	
	#Als de eerste twee bytes niet kloppen, stop met lezen, ga anders verder
	if not startOfImage == bytes((0xFF, 0xD8)): 
		print ("Ingevoerd bestand is geen JPG-bestand.")
		debugLog.write(f"Ingevoerd bestand is geen JPG-bestand\n")
		exit()

	debugLog.write(f"\nFF D8 - Start of Image\n\nAfbeelding succesvol geladen\n")
	#Begin met zoeken naar markers
	searchingForMarkers = True
	
	while searchingForMarkers:
		#Zoek naar de volgende header
		markerID = nextHeaderID(inputFile)
		
		#JFIF-header
		if markerID == 0xE0:
			#Lees alle data
			currentMarkerLength	= int.from_bytes(inputFile.read(2), byteorder = "big")						#Lengte van JFIF-APP0 segment (2 bytes)
			JFIFidentifier		= (inputFile.read(5)).decode("ascii")										#JFIF identifier (5 bytes)
			JFIFversion			= str(ord(inputFile.read(1))) + "." + str(ord(inputFile.read(1))).zfill(2)	#JFIF versie (2 bytes)
			densityUnits		= ord(inputFile.read(1))													#Density Units (1 byte)
			xDensity			= int.from_bytes(inputFile.read(2), byteorder = "big")						#X density (2 bytes)
			yDensity			= int.from_bytes(inputFile.read(2), byteorder = "big")						#Y density (2 bytes)
			widthThumbnail		= ord(inputFile.read(1))													#Breedte thumbnail (byte)
			heightThumbnail		= ord(inputFile.read(1))													#Hoogte thumbnail (byte)
			thumbnailData		= inputFile.read(3 * widthThumbnail * heightThumbnail)						#Thumbnail data (RGB)
			
			#Debug
			debugLog.write("\nFF E0 - JFIF\n")
			debugLog.write(f"\tcurrentMarkerLength = {currentMarkerLength}\n")
			debugLog.write(f"\tJFIFidentifier      = {JFIFidentifier}\n")
			debugLog.write(f"\tJFIFversion         = {JFIFversion}\n")
			debugLog.write(f"\tdensityUnits        = {densityUnits}\n")
			debugLog.write(f"\txDensity            = {xDensity}\n")
			debugLog.write(f"\tyDensity            = {yDensity}\n")
			debugLog.write(f"\twidthThumbnail      = {widthThumbnail}\n")
			debugLog.write(f"\theightThumbnail     = {heightThumbnail}\n")
			debugLog.write(f"\tthumbnailData       = {thumbnailData}\n")
			
		#Exif-header
		elif markerID == 0xE1:
			#Lees alle data
			currentMarkerLength	= int.from_bytes(inputFile.read(2), byteorder = "big")						#Lengte van JFIF-APP0 segment (2 bytes)
			ExifIdentifier		= (inputFile.read(5)).decode("ascii")										#Exif identifier (5 bytes)
			padding				= inputFile.read(1)															#Waarschijnlijk padding (1 byte)
			
			#TIFF-header
			byteOrder			= (inputFile.read(2)).decode("ascii")										#Byte order van alle komende waardes in de TIFF-header
			#Process byte order
			if byteOrder == "II":
				byteOrder = "little"
			elif byteOrder == "MM":
				byteOrder = "big"
			else:
				print ("De Exif-header is corrupt.")
				exit()
			
			#Verder met TIFF-header
			TIFFidentifier		= int.from_bytes(inputFile.read(2), byteorder = byteOrder)				#TIFFidentifier, moet 42 zijn.
			IFDoffset			= int.from_bytes(inputFile.read(4), byteorder = byteOrder)
			
			if TIFFidentifier != 42:
				print ("De Exif-header is corrupt.")
				exit()
			
			#Ga naar de IFDoffset (de huidige positie in de file - 8: positie van de TIFF-header, + IFDoffset)
			inputFile.seek(inputFile.tell() - 8 + IFDoffset)
			
			#Lees de IFD en alle tags
			tagCount 			= int.from_bytes(inputFile.read(2), byteorder = "big")						#Aantal tags
			
			#Ik lees niet de hele header, aangezien er toch niks met de tags wordt gedaan, dus ik laat de nextHeaderID functie de volgende marker zoeken
			
			#Debug
			debugLog.write("\nFF E1 - Exif\n")
			debugLog.write(f"\tcurrentMarkerLength   = {currentMarkerLength}\n")
			debugLog.write(f"\tExifIdentifier        = {ExifIdentifier}\n")
			debugLog.write(f"\tbyteOrder             = {byteOrder}\n")
			debugLog.write(f"\tTIFFidentifier        = {TIFFidentifier}\n")
			debugLog.write(f"\tIFDoffset             = {IFDoffset}\n")
			debugLog.write(f"\ttagCount              = {tagCount}\n")
			
		#Quantization Table	
		elif markerID == 0xDB:
			#Lees alle data
			currentMarkerLength				= int.from_bytes(inputFile.read(2), byteorder = "big")						#Lengte van dit segment (2 bytes) (waarde is vaak 67 bytes)
			currentQuantizationTableID		= ord(inputFile.read(1))													#Quantization Table ID (1 byte)
			currentQuantizationTableValues	= inputFile.read(currentMarkerLength-3)										#Alle Quantization Table waardes (vaak 64 bytes)
			currentDecodedQuantizationTableValues = entropyDecode(currentQuantizationTableValues, currentMarkerLength-3)
			quantizationTableIDs   .append(currentQuantizationTableID)		#Voeg de waardes toe aan de lijsten
			quantizationTableValues.append(currentDecodedQuantizationTableValues)
			
			#Debug
			debugLog.write("\nFF DB - Quantization Table #{currentQuantizationTableID}\n")
			debugLog.write(f"\tcurrentMarkerLength        = {currentMarkerLength}\n")
			debugLog.write(f"\tcurrentQuantizationTableID = {currentQuantizationTableID}\n")
			debugLog.write(f"\tcurrentDecodedQuantizationTableValues:\n")
			debugLog.write(printListAsTable(currentDecodedQuantizationTableValues, 8, 8, '\t'))
			
		#Informatie over de afbeelding zelf
		elif markerID == 0xC0:
			#Lees alle data
			currentMarkerLength				= int.from_bytes(inputFile.read(2), byteorder = "big")						#Lengte van dit segment (2 bytes)
			bitsPerPixel					= ord(inputFile.read(1))													#Bits per pixel van de afbeelding (1 byte)
			height							= int.from_bytes(inputFile.read(2), byteorder = "big")						#Hoogte afbeelding in pixels (2 bytes)
			width							= int.from_bytes(inputFile.read(2), byteorder = "big")						#Breedte afbeelding in pixels (2 bytes)
			channelCount					= ord(inputFile.read(1))													#Aantal channels (1 byte)
			for counter in range(channelCount):
				channelIdentifiers          .append(ord(inputFile.read(1))) #Lees een byte en voeg die toe aan de channelIdentifiers list
				samplingFactors             .append(ord(inputFile.read(1))) #Lees een byte en voeg die toe aan de samplingFactors list
				channelQuantizationTableIDs .append(ord(inputFile.read(1))) #Lees een byte en voeg die toe aan de quantizationTablesChannels list
			downsamplingResolution = [(samplingFactors[0] >> 4), (samplingFactors[0] & 0x0F)]	#De eerste 4 bits en de laatste 4 bits, bijv [2x1] of [2x2] of [1x1]
			amountOfYComponentsForOneColorComponent = downsamplingResolution[0] * downsamplingResolution[1]
			blockLength = amountOfYComponentsForOneColorComponent + 2
			
			#Debug
			debugLog.write("\nFF C0 - Informatie over de afbeelding\n")
			debugLog.write(f"\tcurrentMarkerLength  = {currentMarkerLength}\n")
			debugLog.write(f"\tbitsPerPixel         = {bitsPerPixel}\n")
			debugLog.write(f"\theight               = {height}\n")
			debugLog.write(f"\twidth                = {width}\n")
			debugLog.write(f"\tchannelCount         = {channelCount}\n")
			for x in range(channelCount):
				debugLog.write(f"\tChannel {x}            = {[channelIdentifiers[x], samplingFactors[x], channelQuantizationTableIDs[x]]}\n")
		
		#Huffmantabellen
		elif markerID == 0xC4:
			#Lees alle data
			currentMarkerLength						= int.from_bytes(inputFile.read(2), byteorder = "big")	#Lengte van dit segment
			currentHuffmanTableID					= ord(inputFile.read(1))								#Table ID
			currentHuffmanTableBitLengthValueCounts	= inputFile.read(16)									#16 bytes
			currentHuffmanTableSymbols				= inputFile.read(currentMarkerLength-19)				#De size van de complete section, min de segment size, table id en bit lengths
			
			#Binaire codes afleiden
			currentBinaryCodes = list()
			currentBitLength = 0
			currentBit = "" #Dit is een string, om zo makkelijker meerdere nullen achter elkaar te kunnen zetten
			
			#Voor elke bitlength, voer het stappenplan uit zoals beschreven in het kopje Opbouw JPG-bestand, sectie Huffman-tabellen
			for bitLengthValueCount in currentHuffmanTableBitLengthValueCounts:
				currentBitLength += 1
				
				if bitLengthValueCount == 0: #als het aantal waardes voor deze bitlengte nul is, voeg dan alleen een 0 toe aan het einde van de waarde
					currentBit += "0"
					
				else: #Als het aantal waardes niet nul is,
					currentBit += "0" #voeg dan een nul toe aan het einde van de waarde
					currentBit = int(currentBit, 2) #(Convert van string naar getal, zodat optellen beter gaat)
					
					for counter in range(bitLengthValueCount): #Voer deze loop uit voor het aantal waardes dat er zijn
						currentBinaryCodes.append(bin(currentBit)[2:].zfill(currentBitLength)) #De huidige bitstring is een binaire code, voeg hem toe aan de lijst
						currentBit += 1 #Increment de waarde
						
					currentBit = (bin(currentBit)[2:].zfill(currentBitLength)) #Convert terug naar bitstring
			binaryCodesTables.append(currentBinaryCodes)
			binaryCodesTablesValues.append(currentHuffmanTableSymbols)
			binaryCodesTablesIDs.append(currentHuffmanTableID)
			
			#Debug
			debugLog.write("\nFF C4 - Huffmantabel\n")
			debugLog.write(f"\tcurrentMarkerLength   = {currentMarkerLength}\n")
			debugLog.write(f"\tcurrentHuffmanTableID = {currentHuffmanTableID}\n")
			debugLog.write("\tTabelinhoud:\n")
			for y in range(len(currentBinaryCodes)):
				debugLog.write(f"\t\t{currentBinaryCodes[y]} = {currentHuffmanTableSymbols[y]}\n")
			
		#Data scan
		elif markerID == 0xDA:
			#Lees alle data
			currentMarkerLength				= int.from_bytes(inputFile.read(2), byteorder = "big")						#Lengte van dit segment (2 bytes)
			channelCount					= ord(inputFile.read(1))													#Aantal channels (1 byte)
			for counter in range(channelCount):																			
				DCTchannelSelectorList.append(ord(inputFile.read(1)))				#Lees de selector en stop die in de lijst
				
				currentTableSelectors = hex(ord(inputFile.read(1)))[2:].zfill(2)	#Lees de volgende byte
				DCTtablesSelectorListDC.append(int(currentTableSelectors[0]))		#En splits hem in twee delen voor de DC en AC lijsten
				DCTtablesSelectorListAC.append(int(currentTableSelectors[1]) + 16)	#Plus 16 want de AC tabellen beginnen vanaf 0x10 (16)
			
			spectralSelection = [ord(inputFile.read(1)), ord(inputFile.read(1))] #2 bytes, meestal 0, 63
			successiveApproximation = ord(inputFile.read(1))
			imageDataRange.append(inputFile.tell())
			
			#Debug
			debugLog.write("\nFF DA - Data scan\n")
			debugLog.write(f"\tcurrentMarkerLength     = {currentMarkerLength}\n")
			debugLog.write(f"\tchannelCount            = {channelCount}\n")
			for x in range(channelCount):
				debugLog.write(f"\tChannel {x}               = {[DCTtablesSelectorListDC[x], DCTtablesSelectorListDC[x]]}\n")
			debugLog.write(f"\tspectralSelection       = {spectralSelection}\n")
			debugLog.write(f"\tsuccessiveApproximation = {successiveApproximation}\n")
		
		#Einde van de file
		elif markerID == 0xD9:
			#Stop
			imageDataRange.append(inputFile.tell() - 2)
			searchingForMarkers = False
			
			#Debug
			debugLog.write("\nFF D9 - End of Image\n")
		
	#Na het analyseren van alle headers, tabellen, en informatie van de file, is het tijd om de gecomprimeerde image data te gaan lezen
	inputFile.seek(imageDataRange[0]) #Ga naar het begin van die data
	imageDataSize = imageDataRange[1]-imageDataRange[0]
	rawCompressedImageDataBytes = inputFile.read(imageDataSize) #Lees alle data
	
	#Vervang alle FF 00's door FF
	rawCompressedImageDataBytes = rawCompressedImageDataBytes.replace(b'\xff\x00', b'\xff')
	
	#Lees al die data en stop het in een lange byte string
	readPosition = 0
	fillImageDataBuffer()	#Vul de buffer
	
	#Voor elk kanaal:
	componentIndex = 0
	DCTtables = list()
	totalDCTvalues = spectralSelection[1] - spectralSelection[0] + 1#63-0 + 1 = 64
	blockCount = ceil(width/8) * ceil(height / 8) #Als de resolutie niet een veelvoud van 8 is in een of meer richtingen, dan wordt de afbeelding, als je alleen kijkt naar de blokken, aan de rechterkant en onderkant uitgerekt totdat het wel past. (Vandaar ceil, wat betekent dat er naar boven wordt afgerond)
	
	print ("Data lezen...")
	blockNumber = 0
	while blockNumber < blockCount:
		for componentIndex in range(len(channelIdentifiers)):
			if componentIndex == 0:
				amountOfTimesToRunTheCodeBelow = amountOfYComponentsForOneColorComponent
			else:
				amountOfTimesToRunTheCodeBelow = 1
			for runCount in range (amountOfTimesToRunTheCodeBelow):
				currentDCTvalues = list()
				#Lees de DC-waarde:
				decodeNextDCTvalue(componentIndex, "DC")
				
				#Lees de AC-waardes:
				while len(currentDCTvalues) < totalDCTvalues:
					decodeNextDCTvalue(componentIndex, "AC")
		blockNumber += amountOfYComponentsForOneColorComponent
		clearline()
		print ("Data lezen... " + str(round(100*blockNumber/blockCount)) + "%")
		#input()
		
	counter = 0
	mainSurface = pygame.Surface((width, height))
	xBlock = 0
	yBlock = 0
	
	print ("Afbeelding samenstellen...")
	#Chroma subsampling behandelen 
	#Stel de subsampling is 2x1, dan heeft de decodeNextDCTvalue-routine blokken van 16x8 opgeslagen, die moeten we splitsen
	blockResolution = [x * 8 for x in downsamplingResolution]
	splitBlocks = list()
	
	#Doe niks als er geen subsampling is
	if downsamplingResolution == list([1, 1]):
		splitBlocks = blocks
	else:
		#Voor elke blok van resolutie blockResolution:
		for block in blocks:
			#Combineer de Y-blokken door de ze tegen elkaar te 'plakken'
			if downsamplingResolution == list([2, 1]):
				currentYblock = np.concatenate((block[0], block[1]), axis=1)
			elif downsamplingResolution == list([1, 2]):
				currentYblock = np.concatenate((block[0], block[1]), axis=0)
			elif downsamplingResolution == list([2, 2]):
				currentYblockPart1 = np.concatenate((block[0], block[1]), axis=1)
				currentYblockPart2 = np.concatenate((block[2], block[3]), axis=1)
				currentYblock = np.concatenate((currentYblockPart1, currentYblockPart2), axis=0)
			
			#Stretch de kleurenwaardes naar blockResolution		
			#De kleurenwaardes komen na de Y waardes, dus na amountOfYComponentsForOneColorComponent waardes, dus op index amountOfYComponentsForOneColorComponent
			currentCbBlock = zoom(block[amountOfYComponentsForOneColorComponent],   (downsamplingResolution[1], downsamplingResolution[0]))
			currentCrBlock = zoom(block[amountOfYComponentsForOneColorComponent+1], (downsamplingResolution[1], downsamplingResolution[0]))
			
			#Stel de blokken samen
			currentBlock = list()
			currentBlock.append(currentYblock)
			currentBlock.append(currentCbBlock)
			currentBlock.append(currentCrBlock)
			splitBlocks.append(currentBlock)
			
	for block in splitBlocks:
		#print (blockResolution)
		convertedBlock = convert_YCbCr_block_to_RGB_image(block)
		blockSurface = pygame.image.fromstring(convertedBlock, (blockResolution), "RGB");
		mainSurface.blit(blockSurface, (xBlock, yBlock))
		counter += 1
		xBlock += blockResolution[0]
		if xBlock >= width:
			xBlock = 0
			yBlock += blockResolution[1]
		clearline()
		print ("Afbeelding samenstellen... " + str(round(100*counter/len(blocks))) + "%")
	
	pygame.image.save(mainSurface, outputFilename)

#Zoeken naar de volgende header in een JPG-bestand
def nextHeaderID(inputFile):
	searchForMarker = 0
	while searchForMarker != 0xFF: 						#Blijf zoeken totdat je 0xFF tegenkomt
		try:
			searchForMarker = ord(inputFile.read(1))	#Lees 1 byte
		except TypeError: 								#Als een byte niet kan worden gelezen
			searchForMarker = 0 						#Zet dit getal dan op 0
	markerID = ord(inputFile.read(1)) 					#Als een marker is gevonden, lees de eerstvolgende byte,
	return markerID 									#en return die waarde.

#Decodeer één waarde van de compressed image data
def decodeNextDCTvalue(componentIndex, DCorAC):
	global DCTchannelSelectorList
	global DCTtablesSelectorListDC
	global DCTtablesSelectorListAC
	global binaryCodesTablesValues
	global rawCompressedImageDataBinary
	global currentDCTvalues
	global DCTvaluesLeft
	global channelIdentifiers
	global currentBlock
	global blockLength
	global previousDCcomponents
			
	#Vul de buffer tot hij vol is
	if int(len(rawCompressedImageDataBinary) / 8) < 512:
		fillImageDataBuffer()
		
	#Als de component niet is gevonden, is er een fout opgetreden
	if componentIndex < 0: 
		print ("Fout bij het decoderen van de DCT-tabellen.")
		exit()
		
	if DCorAC == "DC": #Als dit de DC waarde is, gebruik dan de DC tabellen
		currentHuffmanTableID 		= DCTtablesSelectorListDC[componentIndex]			#Vind de bijbehorende DC Huffman table ID
	else:
		currentHuffmanTableID 		= DCTtablesSelectorListAC[componentIndex]			#Vind de bijbehorende AC Huffman table ID
		
	currentHuffmanTableIndex 	= binaryCodesTablesIDs.index(currentHuffmanTableID)		#Vind de bijbehorende Huffman table index in de lijst
	currentBinaryCodes			= binaryCodesTables[currentHuffmanTableIndex]			#Vind de bijbehorende binary codes
	currentValues				= binaryCodesTablesValues[currentHuffmanTableIndex]		#Vind de bijbehorende waardes
	
	#Loop door alle Huffman-codes en kijk met welke code de buffer op dit moment begint	
	for counter in range(len(currentBinaryCodes)):
		#Als de code gevonden is
		if rawCompressedImageDataBinary.startswith(currentBinaryCodes[counter]):
				print (currentBinaryCodes[counter])
				#Verwijder de bitstring van de buffer
				amountToCutFromBinaryString = len(currentBinaryCodes[counter])
				rawCompressedImageDataBinary = rawCompressedImageDataBinary[amountToCutFromBinaryString:]
				
				#Als het een DC-waarde is
				if DCorAC == "DC":
					#Als de huffmanwaarde 0x00 is
					if currentValues[counter] == 0x00:
						#Dan is de DC-waarde 0 (End of Block)
						currentDCTvalue = 0
						DCTvaluesLeft -= 1
					#Als de huffmanwaarde niet 0x00 is
					else:
						#Vind het juiste aantal bits
						currentDCTvalueBitLength = currentValues[counter]
						
						#Lees het juiste aantal bits
						bitsThatWereReadNext = rawCompressedImageDataBinary[:currentDCTvalueBitLength]
						
						#Decode de gelezen bitstring
						if bitsThatWereReadNext[0] == "1":
							#Maak van de bit string via normale methodes een positief getal
							currentDCTvalue = int(bitsThatWereReadNext, 2)				
						else:
							#Maak van de bit string via deze formule: -(2^n) + 1 + getal
							currentDCTvalue = -(1 << currentDCTvalueBitLength) + 1 + int(bitsThatWereReadNext, 2)
							
						#Snij ook dit deel van de binary string eraf
						rawCompressedImageDataBinary = rawCompressedImageDataBinary[currentDCTvalueBitLength:]		
						
					#Pas DPCM toe
					currentDCTvalue += previousDCcomponents[componentIndex]
					
					#Voeg de waarde toe aan de DCT-tabel
					currentDCTvalues.append(currentDCTvalue)
					
					#Bereid de volgende DC-waarde voor op DPCM-correctie
					previousDCcomponents[componentIndex] = currentDCTvalue
	
					#Deze cycle van de loop is nu klaar
					break
						
				#Als het een AC-waarde is
				elif DCorAC == "AC":
					#Als de huffmanwaarde 0x00 is
					if currentValues[counter] == 0x00:
						#Vul de rest van de tabel met nullen
						while (len(currentDCTvalues) < 64):
							currentDCTvalues.append(0)
							DCTvaluesLeft -= 1
					
					#Als de huffmanwaarde 0xF0 is
					elif currentValues[counter] == 0xF0:
						#Voeg 16 nullen toe
						for counter in range(16):
							currentDCTvalues.append(0)
							DCTvaluesLeft -= 1
					
					#Als dat allemaal behandeld is
					else:
						#Vind de huffmanwaarde ([2:] om de "0x" uit de verkregen string te verwijderen, vervolgens zfill(2) om ervoor te zorgen dat ie altijd 2 digits lang is
						huffmanValue = hex(currentValues[counter])[2:].zfill(2)
						
						#De zero run length is de eerste digit van de huffmanwaarde, als een int
						currentZRL = int(huffmanValue[0], 16)
						
						#De bit length is de tweede digit van de huffmanwaarde, als een int
						currentDCTvalueBitLength = int(huffmanValue[1], 16)
						
						#Voeg currentZRL aantal nullen toe
						while currentZRL > 0:
							currentDCTvalues.append(0)
							DCTvaluesLeft -= 1
							currentZRL -= 1
						
						#Lees het juiste aantal bits
						bitsThatWereReadNext = rawCompressedImageDataBinary[:currentDCTvalueBitLength]
						
						#Decode de gelezen bitstring
						if bitsThatWereReadNext[0] == "1":
							#Maak van de bit string via normale methodes een positief getal
							currentDCTvalue = int(bitsThatWereReadNext, 2)				
						else:
							#Maak van de bit string via deze formule: -(2^n) + 1 + getal
							currentDCTvalue = -(1 << currentDCTvalueBitLength) + 1 + int(bitsThatWereReadNext, 2)
						
						#Voeg de waarde toe aan de DCT-tabel
						currentDCTvalues.append(currentDCTvalue)
						
						#Snij ook dit deel van de binary string eraf
						rawCompressedImageDataBinary = rawCompressedImageDataBinary[currentDCTvalueBitLength:]	
					
					#Als we klaar zijn met dit blok
					if len(currentDCTvalues) == 64:
						#Entropy decoderen
						currentDCTvaluesDecoded = entropyDecode(currentDCTvalues, 64)
						
						#Dequantizen
						currentDCTvaluesDequantized = dequantize(currentDCTvaluesDecoded, quantizationTableValues[0])
						
						#Tabel klaarmaken voor inverse DCT door hem in het juiste formaat te zetten
						currentDCTvaluesTransformed = reshape (currentDCTvaluesDequantized, (8,8))
						
						#Inverse Discrete Cosinustransformatie uitvoeren (zie /lib/helperFunctions.py)
						currentDCTvaluesTransformed = idct2d(currentDCTvaluesTransformed)
						
						#Decentreren (zie verslag, Stappen JPEG-compressie: Discrete Cosinustransformatie)
						currentDCTvaluesDecentered = decenter(currentDCTvaluesTransformed)
						currentBlock.append(currentDCTvaluesDecentered)
						
						#Als alle componenten van dit specifieke blok zijn gedecodeerd, voeg hem dan toe aan de block array
						if len(currentBlock) == blockLength:
							blocks.append(currentBlock)
							currentBlock = list()
							
					#Deze cycle van de loop is nu klaar
					break
	return



#Vul de buffer
def fillImageDataBuffer():
	global rawCompressedImageDataBytes
	global rawCompressedImageDataBinary
	global readPosition
	bytesToAdd = 1024 - int(len(rawCompressedImageDataBinary) / 8) #Vul door tot er 1024 bytes in zitten, als dat kan

	#Convert het naar een lange binary string
	for counter in range(bytesToAdd):
		try:
			rawCompressedImageDataBinary += bin(rawCompressedImageDataBytes[readPosition])[2:].zfill(8) #[2:] wil zeggen dat de eerste tekens eraf worden gekapt. Dit is nodig omdat de bin(getal) functie een string geeft die begint met "0b", en dat stuk wil ik niet hebben. zfill(8) betekent dat je altijd 8 bits per byte hebt, en dat dit niet gebeurt: 00010000 -> 10000
			readPosition += 1
		except IndexError:
			break
main()