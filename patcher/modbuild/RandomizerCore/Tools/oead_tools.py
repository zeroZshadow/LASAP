from RandomizerCore.Tools import gsheet


def readSheet(sheetFile):
	with open(sheetFile, 'rb') as file:
		sheet = gsheet.parse(file.read())

	return {'alignment': sheet.alignment, 'hash': sheet.hash, 'name': sheet.name, 'root_fields': sheet.root_fields, 'values': sheet.values}


def writeSheet(sheetFile, sheet):
	newSheet = gsheet.Sheet()
	newSheet.alignment = sheet['alignment']
	newSheet.hash = sheet['hash']
	newSheet.name = sheet['name']
	newSheet.root_fields = sheet['root_fields']

	newSheet.values = sheet['values']

	with open(sheetFile, 'wb') as file:
		file.write(newSheet.to_binary())


def parseStruct(struct, k=None):
	result = {}
	for k,v in struct.items():
		if type(v) == gsheet.Struct:
			result[k] = parseStruct(v)
		elif type(v) == gsheet.StructArray:
			result[k] = parseStructArray(v)
		else:
			result[k] = v

	return result


def parseStructArray(l):
	result = []
	for s in l:
		result.append(parseStruct(s))
	
	return result


def dictToStruct(d):
	for k in d:
		if type(d[k]) == dict:
			d[k] = dictToStruct(d[k])
		elif type(d[k]) == list:
			d[k] = listToSheetArray(k, d[k])

	return gsheet.Struct(d)


def listToSheetArray(name, l):
	if len(l) > 0:
		if type(l[0]) == dict:
			new = gsheet.StructArray()
		elif type(l[0]) == bool:
			new = gsheet.BoolArray()
		elif type(l[0]) == int:
			new = gsheet.IntArray()
		elif type(l[0]) == float:
			new = gsheet.FloatArray()
		elif type(l[0]) == str:
			new = gsheet.StringArray()
		for s in l:
			if type(s) == dict:
				d = dictToStruct(s)
				new.append(d)
			# elif type(s) == list:
			# 	for k in s:
			# 		new.append(listToSheetArray(k, s))
	else:
		new = gsheet.StructArray()
	
	return new


def createField(name, type_name, type, offset, flags=None):
	field = gsheet.Field()

	field.name = name
	field.type_name = type_name
	field.type = type
	
	if flags:
		field.flags = flags

	if field.type is gsheet.Field.Type.String:
		field.inline_size = 0x10
	
	if field.type is gsheet.Field.Type.Int:
		field.inline_size = 0x4
	
	if field.type is gsheet.Field.Type.Struct:
		field.inline_size = 0x10
		field.fields.append(createField('category', 'ConditionCategory', gsheet.Field.Type.Int, 16, gsheet.Field.Flag.IsEnum))
		field.fields.append(createField('parameter', 'string', gsheet.Field.Type.String, 16))
	
	field.data_size = field.inline_size
	field.offset_in_value = offset
	# field.x11 = 0

	return field


### CONDITIONS GSHEET
# Return and create an empty structure for an element in the Conditions datasheet
# checks is a list of (category, paramter) tuples
def createCondition(name, checks):
	condition = {'symbol': name, 'conditions': gsheet.StructArray()}
	for category, parameter in checks:
		condition['conditions'].append({'category': category, 'parameter': parameter})

	return condition


### NPC GSHEET
# # creates a new value for the npc gsheet
# def newNpcFromDict(npc):
# 	new = {
# 		'symbol': npc['symbol'],
# 		'graphics': {

# 		}
# 	}
# 	new['symbol'] = npc['symbol']
# 	new['graphics'][]


class SARC:
	def __init__(self, sarcFile: str):
		import oead  # only the asset build reads archives (ap_assets.py)
		with open(sarcFile, 'rb') as f:
			self.reader = oead.Sarc(f.read())
		self.writer = oead.SarcWriter.from_sarc(self.reader)
		oead.SarcWriter.set_endianness(self.writer, oead.Endianness.Little) # Switch uses Little Endian
	
	def repack(self):
		return self.writer.write()[1]
